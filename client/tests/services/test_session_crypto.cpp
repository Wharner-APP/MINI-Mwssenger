#include "crypto/IdentityKeys.h"
#include "services/SessionCrypto.h"

#include <QJsonObject>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

net::PeerKeys publicKeys(const crypto::IdentityState &state)
{
    net::PeerKeys keys;
    keys.identityKey = state.identity.publicKey;
    keys.signedPrekey = state.signedPrekey.pair.publicKey;
    keys.signedPrekeySignature = state.signedPrekey.signature;
    keys.signedPrekeyId = state.signedPrekey.id;
    if (!state.oneTimePrekeys.isEmpty()) {
        keys.oneTimePrekeyId = state.oneTimePrekeys.first().id;
        keys.oneTimePrekey = state.oneTimePrekeys.first().pair.publicKey;
    }
    return keys;
}

} // namespace

class TestSessionCrypto : public QObject
{
    Q_OBJECT

private slots:
    void roundTripBetweenTwoDevices();
    void tamperedCiphertextIsRejected();
    void invalidPrekeySignatureIsRejected();
    void firstMessageCarriesX3dhThenRatchets();
};

void TestSessionCrypto::roundTripBetweenTwoDevices()
{
    const auto alice = crypto::IdentityKeys::generate(1000);
    const auto bob = crypto::IdentityKeys::generate(2000);
    QVERIFY(alice.isOk());
    QVERIFY(bob.isOk());

    const net::PeerKeys bobKeys = publicKeys(bob.value());
    const net::PeerKeys aliceKeys = publicKeys(alice.value());
    SessionCrypto aliceCrypto(alice.value(), [bobKeys](const QString &,
                                                       std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(bobKeys));
    });
    SessionCrypto bobCrypto(bob.value(), [aliceKeys](const QString &,
                                                     std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(aliceKeys));
    });

    bool ready = false;
    aliceCrypto.prepare(QStringLiteral("direct_1"), QStringLiteral("bob"),
                        [&ready](Result<bool> result) { ready = result.isOk(); });
    QVERIFY(ready);

    QJsonObject header;
    const auto sealed = aliceCrypto.encrypt(QStringLiteral("direct_1"),
                                            QByteArrayLiteral("hello bob"), &header);
    QVERIFY(sealed.isOk());
    QVERIFY(sealed.value() != QByteArrayLiteral("hello bob"));

    const auto plain = bobCrypto.decrypt(QStringLiteral("direct_1"), sealed.value(), header);
    QVERIFY(plain.isOk());
    QCOMPARE(plain.value(), QByteArrayLiteral("hello bob"));
}

void TestSessionCrypto::tamperedCiphertextIsRejected()
{
    const auto alice = crypto::IdentityKeys::generate(1000);
    const auto bob = crypto::IdentityKeys::generate(2000);
    const net::PeerKeys bobKeys = publicKeys(bob.value());
    const net::PeerKeys aliceKeys = publicKeys(alice.value());
    SessionCrypto aliceCrypto(alice.value(), [bobKeys](const QString &,
                                                       std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(bobKeys));
    });
    SessionCrypto bobCrypto(bob.value(), [aliceKeys](const QString &,
                                                     std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(aliceKeys));
    });
    bool ready = false;
    aliceCrypto.prepare(QStringLiteral("direct_2"), QStringLiteral("bob"),
                        [&ready](Result<bool> result) { ready = result.isOk(); });
    QVERIFY(ready);

    QJsonObject header;
    auto sealed = aliceCrypto.encrypt(QStringLiteral("direct_2"), QByteArrayLiteral("secret"),
                                      &header);
    QVERIFY(sealed.isOk());
    sealed.value()[sealed.value().size() - 1] =
        sealed.value().at(sealed.value().size() - 1) ^ 0x01;
    QVERIFY(bobCrypto.decrypt(QStringLiteral("direct_2"), sealed.value(), header).isError());
}

void TestSessionCrypto::invalidPrekeySignatureIsRejected()
{
    const auto alice = crypto::IdentityKeys::generate(1000);
    const auto bob = crypto::IdentityKeys::generate(2000);
    net::PeerKeys bobKeys = publicKeys(bob.value());
    bobKeys.signedPrekeySignature[0] = bobKeys.signedPrekeySignature.at(0) ^ 0x01;

    SessionCrypto aliceCrypto(alice.value(), [bobKeys](const QString &,
                                                       std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(bobKeys));
    });
    bool ready = false;
    QString error;
    aliceCrypto.prepare(QStringLiteral("direct_3"), QStringLiteral("bob"),
                        [&](Result<bool> result) {
                            ready = result.isOk();
                            error = result.error();
                        });
    QVERIFY(!ready);
    QCOMPARE(error, QStringLiteral("peer_prekey_signature"));
}

void TestSessionCrypto::firstMessageCarriesX3dhThenRatchets()
{
    const auto alice = crypto::IdentityKeys::generate(1000);
    const auto bob = crypto::IdentityKeys::generate(2000);
    QVERIFY(alice.isOk());
    QVERIFY(bob.isOk());
    const net::PeerKeys bobKeys = publicKeys(bob.value());
    SessionCrypto aliceCrypto(alice.value(), [bobKeys](const QString &,
                                                       std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::ok(bobKeys));
    });
    SessionCrypto bobCrypto(bob.value(), [](const QString &,
                                            std::function<void(Result<net::PeerKeys>)> done) {
        done(Result<net::PeerKeys>::error(QStringLiteral("unused")));
    });

    bool ready = false;
    aliceCrypto.prepare(QStringLiteral("direct_4"), QStringLiteral("bob"),
                        [&ready](Result<bool> result) { ready = result.isOk(); });
    QVERIFY(ready);

    QJsonObject firstHeader;
    const auto first = aliceCrypto.encrypt(QStringLiteral("direct_4"),
                                            QByteArrayLiteral("one"), &firstHeader);
    QVERIFY2(first.isOk(), qPrintable(first.error()));
    QCOMPARE(firstHeader.value(QStringLiteral("session_v")).toInt(), 2);
    QVERIFY(firstHeader.contains(QStringLiteral("x3dh")));
    QVERIFY(firstHeader.contains(QStringLiteral("dr")));
    const auto opened = bobCrypto.decrypt(QStringLiteral("direct_4"), first.value(), firstHeader);
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));
    QCOMPARE(opened.value(), QByteArrayLiteral("one"));

    QJsonObject secondHeader;
    const auto second = aliceCrypto.encrypt(QStringLiteral("direct_4"),
                                            QByteArrayLiteral("two"), &secondHeader);
    QVERIFY(second.isOk());
    QVERIFY(!secondHeader.contains(QStringLiteral("x3dh")));
    const auto openedSecond =
        bobCrypto.decrypt(QStringLiteral("direct_4"), second.value(), secondHeader);
    QVERIFY2(openedSecond.isOk(), qPrintable(openedSecond.error()));
    QCOMPARE(openedSecond.value(), QByteArrayLiteral("two"));
}

QTEST_MAIN(TestSessionCrypto)
#include "test_session_crypto.moc"
