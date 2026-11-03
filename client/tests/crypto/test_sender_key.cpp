#include "crypto/SenderKey.h"
#include "crypto/Sodium.h"

#include <QJsonObject>
#include <QtTest>

using namespace mini;
using namespace mini::crypto;

class TestSenderKey : public QObject
{
    Q_OBJECT

private slots:
    void roundTripKeepsPlaintextAndAdvances();
    void rejectsTamperedCiphertext();
    void rejectsSignatureFromAnotherSender();
    void rejectsReplayedOldMessage();
    void handlesSkippedIterations();
    void distributionCarriesNoPrivateKey();
    void rotationBumpsKeyId();
};

void TestSenderKey::roundTripKeepsPlaintextAndAdvances()
{
    QVERIFY(Sodium::init());
    auto generated = SenderKey::generate(1);
    QVERIFY(generated.isOk());
    SenderKeyState sender = generated.value();
    SenderKeyState receiver = SenderKey::parseDistribution(
                                  SenderKey::serializeDistribution(QStringLiteral("g1"),
                                                                   QStringLiteral("u1"),
                                                                   sender),
                                  QStringLiteral("u1"))
                                  .value();
    QVERIFY(receiver.isValid());
    QVERIFY(!receiver.canSign());

    QJsonObject header;
    auto sealed = SenderKey::encrypt(sender, QStringLiteral("g1"), QStringLiteral("u1"),
                                     QByteArrayLiteral("hello group"), &header);
    QVERIFY(sealed.isOk());
    QCOMPARE(sender.iteration, 1u);
    QCOMPARE(header.value(QStringLiteral("iteration")).toInt(), 0);
    QVERIFY(!header.value(QStringLiteral("sig")).toString().isEmpty());

    auto parsed = SenderKeyHeader::fromJson(header);
    QVERIFY(parsed.isOk());
    const QByteArray signature =
        QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1());
    auto plain = SenderKey::decrypt(receiver, parsed.value(), sealed.value(), signature);
    QVERIFY2(plain.isOk(), qPrintable(plain.error()));
    QCOMPARE(plain.value(), QByteArrayLiteral("hello group"));
    QCOMPARE(receiver.iteration, 1u);

    // Second message with the advanced chain.
    QJsonObject header2;
    auto sealed2 = SenderKey::encrypt(sender, QStringLiteral("g1"), QStringLiteral("u1"),
                                      QByteArrayLiteral("second"), &header2);
    QVERIFY(sealed2.isOk());
    auto parsed2 = SenderKeyHeader::fromJson(header2);
    QVERIFY(parsed2.isOk());
    auto plain2 = SenderKey::decrypt(
        receiver, parsed2.value(), sealed2.value(),
        QByteArray::fromBase64(header2.value(QStringLiteral("sig")).toString().toLatin1()));
    QVERIFY(plain2.isOk());
    QCOMPARE(plain2.value(), QByteArrayLiteral("second"));
}

void TestSenderKey::rejectsTamperedCiphertext()
{
    auto sender = SenderKey::generate(1).value();
    const QByteArray distribution =
        SenderKey::serializeDistribution(QStringLiteral("g"), QStringLiteral("u"), sender);
    auto receiver = SenderKey::parseDistribution(distribution, QStringLiteral("u")).value();

    QJsonObject header;
    auto sealed = SenderKey::encrypt(sender, QStringLiteral("g"), QStringLiteral("u"),
                                     QByteArrayLiteral("secret"), &header);
    QVERIFY(sealed.isOk());
    QByteArray tampered = sealed.value();
    tampered[0] = static_cast<char>(tampered.at(0) ^ 0x5a);
    auto plain = SenderKey::decrypt(
        receiver, SenderKeyHeader::fromJson(header).value(), tampered,
        QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1()));
    QVERIFY(plain.isError());
    QCOMPARE(plain.error(), QStringLiteral("sender_key_signature_invalid"));
}

void TestSenderKey::rejectsSignatureFromAnotherSender()
{
    auto sender = SenderKey::generate(1).value();
    auto receiver = SenderKey::parseDistribution(
                        SenderKey::serializeDistribution(QStringLiteral("g"),
                                                         QStringLiteral("u"), sender),
                        QStringLiteral("u"))
                        .value();
    auto attacker = SenderKey::generate(1).value();

    QJsonObject header;
    auto sealed = SenderKey::encrypt(attacker, QStringLiteral("g"), QStringLiteral("u"),
                                     QByteArrayLiteral("forged"), &header);
    QVERIFY(sealed.isOk());
    auto plain = SenderKey::decrypt(
        receiver, SenderKeyHeader::fromJson(header).value(), sealed.value(),
        QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1()));
    QVERIFY(plain.isError());
}

void TestSenderKey::rejectsReplayedOldMessage()
{
    auto sender = SenderKey::generate(1).value();
    auto receiver = SenderKey::parseDistribution(
                        SenderKey::serializeDistribution(QStringLiteral("g"),
                                                         QStringLiteral("u"), sender),
                        QStringLiteral("u"))
                        .value();
    QJsonObject header;
    auto first = SenderKey::encrypt(sender, QStringLiteral("g"), QStringLiteral("u"),
                                    QByteArrayLiteral("one"), &header);
    QVERIFY(first.isOk());
    const QByteArray signature =
        QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1());
    auto parsed = SenderKeyHeader::fromJson(header);
    QVERIFY(parsed.isOk());

    auto accepted = SenderKey::decrypt(receiver, parsed.value(), first.value(), signature);
    QVERIFY(accepted.isOk());
    // Replaying the same frame after the chain advanced must not decrypt again.
    auto replay = SenderKey::decrypt(receiver, parsed.value(), first.value(), signature);
    QVERIFY(replay.isError());
    QCOMPARE(replay.error(), QStringLiteral("sender_key_message_old"));
}

void TestSenderKey::handlesSkippedIterations()
{
    auto sender = SenderKey::generate(1).value();
    auto receiver = SenderKey::parseDistribution(
                        SenderKey::serializeDistribution(QStringLiteral("g"),
                                                         QStringLiteral("u"), sender),
                        QStringLiteral("u"))
                        .value();
    QVector<QPair<QJsonObject, QByteArray>> messages;
    for (int i = 0; i < 3; ++i) {
        QJsonObject header;
        auto sealed = SenderKey::encrypt(sender, QStringLiteral("g"), QStringLiteral("u"),
                                         QByteArray::number(i), &header);
        messages.append(qMakePair(header, sealed.value()));
    }
    // Deliver 2, 0, 1. The receiver caches skipped keys.
    for (int index : {2, 0, 1}) {
        const auto &header = messages.at(index).first;
        auto plain = SenderKey::decrypt(
            receiver, SenderKeyHeader::fromJson(header).value(), messages.at(index).second,
            QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1()));
        QVERIFY2(plain.isOk(), qPrintable(plain.error()));
        QCOMPARE(plain.value(), QByteArray::number(index));
    }
}

void TestSenderKey::distributionCarriesNoPrivateKey()
{
    auto sender = SenderKey::generate(1).value();
    const QByteArray payload = SenderKey::serializeDistribution(
        QStringLiteral("g"), QStringLiteral("u"), sender);
    QVERIFY(!payload.contains(sender.signingPrivate.toBase64()));
    QVERIFY(!payload.contains("signing_private"));
    auto parsed = SenderKey::parseDistribution(payload, QStringLiteral("someone_else"));
    QVERIFY(parsed.isError());
    QCOMPARE(parsed.error(), QStringLiteral("sender_key_sender_mismatch"));
}

void TestSenderKey::rotationBumpsKeyId()
{
    auto first = SenderKey::generate(1).value();
    auto second = SenderKey::rotate(first).value();
    QCOMPARE(second.keyId, 2u);
    QVERIFY(second.chainKey != first.chainKey);
    QVERIFY(second.signingPublic != first.signingPublic);
}

QTEST_MAIN(TestSenderKey)
#include "test_sender_key.moc"
