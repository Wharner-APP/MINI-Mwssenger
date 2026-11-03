#include "crypto/DoubleRatchet.h"

#include <QVector>
#include <QtTest>

#include <sodium.h>

using mini::Result;
using mini::crypto::DoubleRatchet;
using mini::crypto::RatchetMessage;
using mini::crypto::X25519KeyPair;

namespace {

struct RatchetPair
{
    DoubleRatchet alice;
    DoubleRatchet bob;
};

X25519KeyPair generateKeyPair()
{
    X25519KeyPair pair;
    pair.publicKey = QByteArray(32, Qt::Uninitialized);
    pair.privateKey = QByteArray(32, Qt::Uninitialized);
    crypto_box_keypair(reinterpret_cast<unsigned char *>(pair.publicKey.data()),
                       reinterpret_cast<unsigned char *>(pair.privateKey.data()));
    return pair;
}

bool makePair(RatchetPair &pair)
{
    const QByteArray sharedSecret(32, '\x5a');
    const X25519KeyPair signedPrekey = generateKeyPair();
    const X25519KeyPair ephemeral = generateKeyPair();

    Result<DoubleRatchet> alice =
        DoubleRatchet::initInitiator(sharedSecret, ephemeral, signedPrekey.publicKey);
    Result<DoubleRatchet> bob =
        DoubleRatchet::initResponder(sharedSecret, signedPrekey, ephemeral.publicKey);
    if (alice.isError() || bob.isError()) {
        return false;
    }
    pair.alice = std::move(alice.value());
    pair.bob = std::move(bob.value());
    return true;
}

} // namespace

class TestDoubleRatchet : public QObject
{
    Q_OBJECT

private slots:
    void roundTripBothDirections();
    void emptyPlaintextRoundTrips();
    void thousandPingPongMessages();
    void hundredOutOfOrderMessages();
    void skipBeyondLimitIsRejected();
    void tamperedCiphertextFailsAndKeepsState();
    void headerIsAuthenticated();
    void serializedMessageRoundTrips();
    void responderCannotSendFirst();
    void associatedDataIsBound();
    void resetWipesSession();
    void previousChainMessageSurvivesDhRatchet();
};

void TestDoubleRatchet::roundTripBothDirections()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    QVERIFY(pair.alice.isActive());
    QVERIFY(pair.bob.isActive());

    const Result<RatchetMessage> first = pair.alice.encrypt(QByteArrayLiteral("hello bob"));
    QVERIFY2(first.isOk(), qPrintable(first.error()));
    const Result<QByteArray> opened = pair.bob.decrypt(first.value());
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));
    QCOMPARE(opened.value(), QByteArrayLiteral("hello bob"));

    const Result<RatchetMessage> reply = pair.bob.encrypt(QByteArrayLiteral("hello alice"));
    QVERIFY2(reply.isOk(), qPrintable(reply.error()));
    const Result<QByteArray> replyOpened = pair.alice.decrypt(reply.value());
    QVERIFY2(replyOpened.isOk(), qPrintable(replyOpened.error()));
    QCOMPARE(replyOpened.value(), QByteArrayLiteral("hello alice"));
}

void TestDoubleRatchet::emptyPlaintextRoundTrips()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> message = pair.alice.encrypt(QByteArray());
    QVERIFY2(message.isOk(), qPrintable(message.error()));
    QCOMPARE(message.value().ciphertext.size(), 16);
    const Result<QByteArray> opened = pair.bob.decrypt(message.value());
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));
    QVERIFY(opened.value().isEmpty());
}

void TestDoubleRatchet::thousandPingPongMessages()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));

    for (int i = 0; i < 1000; ++i) {
        const QByteArray outgoing = QByteArrayLiteral("a") + QByteArray::number(i);
        const Result<RatchetMessage> message = pair.alice.encrypt(outgoing);
        QVERIFY2(message.isOk(), qPrintable(message.error()));
        const Result<QByteArray> opened = pair.bob.decrypt(message.value());
        QVERIFY2(opened.isOk(), qPrintable(opened.error()));
        QCOMPARE(opened.value(), outgoing);

        const QByteArray reply = QByteArrayLiteral("b") + QByteArray::number(i);
        const Result<RatchetMessage> replyMessage = pair.bob.encrypt(reply);
        QVERIFY2(replyMessage.isOk(), qPrintable(replyMessage.error()));
        const Result<QByteArray> replyOpened = pair.alice.decrypt(replyMessage.value());
        QVERIFY2(replyOpened.isOk(), qPrintable(replyOpened.error()));
        QCOMPARE(replyOpened.value(), reply);
    }
}

void TestDoubleRatchet::hundredOutOfOrderMessages()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));

    QVector<RatchetMessage> messages;
    messages.reserve(100);
    for (int i = 0; i < 100; ++i) {
        const Result<RatchetMessage> message =
            pair.alice.encrypt(QByteArrayLiteral("m") + QByteArray::number(i));
        QVERIFY2(message.isOk(), qPrintable(message.error()));
        messages.append(message.value());
    }

    QVector<int> order;
    for (int i = 0; i < 100; i += 2) {
        order.append(i);
    }
    for (int i = 99; i >= 1; i -= 2) {
        order.append(i);
    }
    QCOMPARE(order.size(), 100);

    for (const int index : order) {
        const Result<QByteArray> opened = pair.bob.decrypt(messages.at(index));
        QVERIFY2(opened.isOk(), qPrintable(opened.error()));
        QCOMPARE(opened.value(), QByteArrayLiteral("m") + QByteArray::number(index));
    }
    QCOMPARE(pair.bob.skippedKeyCount(), 0);
}

void TestDoubleRatchet::skipBeyondLimitIsRejected()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));

    const Result<RatchetMessage> first = pair.alice.encrypt(QByteArrayLiteral("zero"));
    QVERIFY2(first.isOk(), qPrintable(first.error()));
    QVERIFY(pair.bob.decrypt(first.value()).isOk());

    // n = 1 .. 1002; the last one would need a jump of 1001 > MAX_SKIP (1000).
    QVector<RatchetMessage> messages;
    for (int i = 1; i <= 1002; ++i) {
        const Result<RatchetMessage> message =
            pair.alice.encrypt(QByteArrayLiteral("x") + QByteArray::number(i));
        QVERIFY2(message.isOk(), qPrintable(message.error()));
        messages.append(message.value());
    }

    const Result<QByteArray> tooFar = pair.bob.decrypt(messages.last());
    QVERIFY(tooFar.isError());
    QCOMPARE(tooFar.error(), QStringLiteral("crypto_skip_limit"));

    // The boundary itself is still allowed: n = 1002 requires 1000 stored keys.
    const Result<QByteArray> lastAllowed =
        pair.bob.decrypt(messages.at(messages.size() - 2));
    QVERIFY2(lastAllowed.isOk(), qPrintable(lastAllowed.error()));
    QCOMPARE(lastAllowed.value(), QByteArrayLiteral("x") + QByteArray::number(1001));
}

void TestDoubleRatchet::tamperedCiphertextFailsAndKeepsState()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));

    const Result<RatchetMessage> message = pair.alice.encrypt(QByteArrayLiteral("secret"));
    QVERIFY(message.isOk());
    RatchetMessage tampered = message.value();
    tampered.ciphertext[0] = static_cast<char>(tampered.ciphertext.at(0) ^ 0x01);

    const Result<QByteArray> rejected = pair.bob.decrypt(tampered);
    QVERIFY(rejected.isError());
    QCOMPARE(rejected.error(), QStringLiteral("crypto.aead_failed"));

    // A failed decrypt must not consume the message key or advance the chain.
    const Result<QByteArray> accepted = pair.bob.decrypt(message.value());
    QVERIFY2(accepted.isOk(), qPrintable(accepted.error()));
    QCOMPARE(accepted.value(), QByteArrayLiteral("secret"));
}

void TestDoubleRatchet::headerIsAuthenticated()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> message = pair.alice.encrypt(QByteArrayLiteral("header aad"));
    QVERIFY(message.isOk());

    RatchetMessage wrongDh = message.value();
    wrongDh.header.dhPublic[0] = static_cast<char>(wrongDh.header.dhPublic.at(0) ^ 0x01);
    const Result<QByteArray> dhRejected = pair.bob.decrypt(wrongDh);
    QVERIFY(dhRejected.isError());
    QCOMPARE(dhRejected.error(), QStringLiteral("crypto.ratchet.peer_key"));

    RatchetMessage wrongNumber = message.value();
    wrongNumber.header.messageNumber = 3;
    const Result<QByteArray> numberRejected = pair.bob.decrypt(wrongNumber);
    QVERIFY(numberRejected.isError());
    QCOMPARE(numberRejected.error(), QStringLiteral("crypto.aead_failed"));
}

void TestDoubleRatchet::serializedMessageRoundTrips()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> message = pair.alice.encrypt(QByteArrayLiteral("wire"));
    QVERIFY(message.isOk());

    const QByteArray wire = message.value().serialize();
    QVERIFY(!wire.isEmpty());
    const Result<RatchetMessage> parsed = RatchetMessage::parse(wire);
    QVERIFY2(parsed.isOk(), qPrintable(parsed.error()));
    QCOMPARE(parsed.value().header.messageNumber, message.value().header.messageNumber);

    const Result<QByteArray> opened = pair.bob.decrypt(parsed.value());
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));
    QCOMPARE(opened.value(), QByteArrayLiteral("wire"));
}

void TestDoubleRatchet::responderCannotSendFirst()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> early = pair.bob.encrypt(QByteArrayLiteral("too early"));
    QVERIFY(early.isError());
    QCOMPARE(early.error(), QStringLiteral("crypto.ratchet.inactive"));
}

void TestDoubleRatchet::associatedDataIsBound()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> message =
        pair.alice.encrypt(QByteArrayLiteral("payload"), QByteArrayLiteral("chat-42"));
    QVERIFY(message.isOk());

    const Result<QByteArray> wrongChat =
        pair.bob.decrypt(message.value(), QByteArrayLiteral("chat-41"));
    QVERIFY(wrongChat.isError());
    QCOMPARE(wrongChat.error(), QStringLiteral("crypto.aead_failed"));

    const Result<QByteArray> rightChat =
        pair.bob.decrypt(message.value(), QByteArrayLiteral("chat-42"));
    QVERIFY2(rightChat.isOk(), qPrintable(rightChat.error()));
    QCOMPARE(rightChat.value(), QByteArrayLiteral("payload"));
}

void TestDoubleRatchet::resetWipesSession()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));
    const Result<RatchetMessage> message = pair.alice.encrypt(QByteArrayLiteral("before"));
    QVERIFY(message.isOk());
    QVERIFY(pair.bob.decrypt(message.value()).isOk());

    pair.bob.reset();
    QVERIFY(!pair.bob.isActive());
    const Result<QByteArray> afterReset =
        pair.bob.decrypt(pair.alice.encrypt(QByteArrayLiteral("after")).value());
    QVERIFY(afterReset.isError());
    QCOMPARE(afterReset.error(), QStringLiteral("crypto.ratchet.inactive"));
}

void TestDoubleRatchet::previousChainMessageSurvivesDhRatchet()
{
    RatchetPair pair;
    QVERIFY(makePair(pair));

    const auto m0 = pair.alice.encrypt(QByteArrayLiteral("m0"));
    QVERIFY(m0.isOk());
    QVERIFY(pair.bob.decrypt(m0.value()).isOk());

    const auto r0 = pair.bob.encrypt(QByteArrayLiteral("r0"));
    QVERIFY(r0.isOk());
    QVERIFY(pair.alice.decrypt(r0.value()).isOk());

    const auto m1 = pair.alice.encrypt(QByteArrayLiteral("m1"));
    QVERIFY(m1.isOk());
    QVERIFY(pair.bob.decrypt(m1.value()).isOk());

    // m2 stays in flight while both sides move to the next DH ratchet.
    const auto m2 = pair.alice.encrypt(QByteArrayLiteral("m2"));
    QVERIFY(m2.isOk());

    const auto r1 = pair.bob.encrypt(QByteArrayLiteral("r1"));
    QVERIFY(r1.isOk());
    QVERIFY(pair.alice.decrypt(r1.value()).isOk());

    const auto m3 = pair.alice.encrypt(QByteArrayLiteral("m3"));
    QVERIFY(m3.isOk());
    QVERIFY(pair.bob.decrypt(m3.value()).isOk());

    const auto delayed = pair.bob.decrypt(m2.value());
    QVERIFY2(delayed.isOk(), qPrintable(delayed.error()));
    QCOMPARE(delayed.value(), QByteArrayLiteral("m2"));
}

QTEST_MAIN(TestDoubleRatchet)
#include "test_double_ratchet.moc"
