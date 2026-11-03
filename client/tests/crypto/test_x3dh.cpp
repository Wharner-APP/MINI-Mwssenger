#include "crypto/IdentityKeys.h"
#include "crypto/X3DH.h"

#include <QtTest>

using mini::Result;
using mini::crypto::IdentityKeys;
using mini::crypto::IdentityState;
using mini::crypto::PrekeyBundle;
using mini::crypto::X3DH;
using mini::crypto::X3DHInit;
using mini::crypto::X3DHResult;

namespace {

PrekeyBundle bundleWithoutOneTimePrekey(const PrekeyBundle &source)
{
    PrekeyBundle bundle = source;
    bundle.oneTimePrekeyId = 0;
    bundle.oneTimePrekey.clear();
    return bundle;
}

} // namespace

class TestX3DH : public QObject
{
    Q_OBJECT

private slots:
    void roundTripWithOneTimePrekey();
    void roundTripWithoutOneTimePrekey();
    void signatureIsVerifiedBeforeDh();
    void unknownSignedPrekeyIsRejected();
    void mismatchedInitiatorIdentityChangesSecret();
    void malformedBundleIsRejected();
};

void TestX3DH::roundTripWithOneTimePrekey()
{
    const IdentityState alice = IdentityKeys::generate(1).value();
    const IdentityState bob = IdentityKeys::generate(2).value();
    const PrekeyBundle bundle = IdentityKeys::publicBundle(bob).value();
    QVERIFY(bundle.oneTimePrekeyId != 0);

    const Result<X3DHResult> initiated = X3DH::initiate(bundle, alice.identity);
    QVERIFY2(initiated.isOk(), qPrintable(initiated.error()));
    QCOMPARE(initiated.value().sharedSecret.size(), X3DH::kSharedSecretBytes);
    QCOMPARE(initiated.value().init.identityKey, alice.identity.publicKey);
    QCOMPARE(initiated.value().init.ephemeralKey, initiated.value().ephemeral.publicKey);
    QCOMPARE(initiated.value().init.signedPrekeyId, bundle.signedPrekeyId);
    QCOMPARE(initiated.value().init.oneTimePrekeyId, bundle.oneTimePrekeyId);

    const Result<QByteArray> responded = X3DH::respond(initiated.value().init, bob);
    QVERIFY2(responded.isOk(), qPrintable(responded.error()));
    QCOMPARE(responded.value().size(), X3DH::kSharedSecretBytes);
    QCOMPARE(responded.value(), initiated.value().sharedSecret);
}

void TestX3DH::roundTripWithoutOneTimePrekey()
{
    const IdentityState alice = IdentityKeys::generate(3).value();
    const IdentityState bob = IdentityKeys::generate(4).value();
    const PrekeyBundle bundle =
        bundleWithoutOneTimePrekey(IdentityKeys::publicBundle(bob).value());

    const Result<X3DHResult> initiated = X3DH::initiate(bundle, alice.identity);
    QVERIFY2(initiated.isOk(), qPrintable(initiated.error()));
    QCOMPARE(initiated.value().init.oneTimePrekeyId, quint32(0));

    const Result<QByteArray> responded = X3DH::respond(initiated.value().init, bob);
    QVERIFY2(responded.isOk(), qPrintable(responded.error()));
    QCOMPARE(responded.value(), initiated.value().sharedSecret);
    QCOMPARE(responded.value().size(), X3DH::kSharedSecretBytes);
}

void TestX3DH::signatureIsVerifiedBeforeDh()
{
    const IdentityState alice = IdentityKeys::generate(5).value();
    const IdentityState bob = IdentityKeys::generate(6).value();
    PrekeyBundle bundle = IdentityKeys::publicBundle(bob).value();
    bundle.signedPrekeySignature[0] =
        static_cast<char>(bundle.signedPrekeySignature.at(0) ^ 0x01);

    const Result<X3DHResult> initiated = X3DH::initiate(bundle, alice.identity);
    QVERIFY(initiated.isError());
    QCOMPARE(initiated.error(), QStringLiteral("crypto.x3dh.signature"));
}

void TestX3DH::unknownSignedPrekeyIsRejected()
{
    const IdentityState alice = IdentityKeys::generate(7).value();
    const IdentityState bob = IdentityKeys::generate(8).value();
    const Result<X3DHResult> initiated =
        X3DH::initiate(IdentityKeys::publicBundle(bob).value(), alice.identity);
    QVERIFY(initiated.isOk());

    X3DHInit header = initiated.value().init;
    header.signedPrekeyId = 0xdeadbeefu;
    const Result<QByteArray> responded = X3DH::respond(header, bob);
    QVERIFY(responded.isError());
    QCOMPARE(responded.error(), QStringLiteral("crypto.keys_prekey_missing"));
}

void TestX3DH::mismatchedInitiatorIdentityChangesSecret()
{
    const IdentityState alice = IdentityKeys::generate(9).value();
    const IdentityState bob = IdentityKeys::generate(10).value();
    const IdentityState impostor = IdentityKeys::generate(11).value();
    const Result<X3DHResult> initiated =
        X3DH::initiate(IdentityKeys::publicBundle(bob).value(), alice.identity);
    QVERIFY(initiated.isOk());

    X3DHInit header = initiated.value().init;
    header.identityKey = impostor.identity.publicKey;
    const Result<QByteArray> responded = X3DH::respond(header, bob);
    QVERIFY(responded.isOk());
    QVERIFY(responded.value() != initiated.value().sharedSecret);
}

void TestX3DH::malformedBundleIsRejected()
{
    const IdentityState alice = IdentityKeys::generate(12).value();
    const IdentityState bob = IdentityKeys::generate(13).value();

    PrekeyBundle bundle = IdentityKeys::publicBundle(bob).value();
    bundle.oneTimePrekey.clear();
    bundle.oneTimePrekeyId = 123;
    QVERIFY(X3DH::initiate(bundle, alice.identity).isError());

    bundle = IdentityKeys::publicBundle(bob).value();
    bundle.identityKey = QByteArray(16, '\x01');
    QVERIFY(X3DH::initiate(bundle, alice.identity).isError());

    const Result<QByteArray> responded =
        X3DH::respond(X3DHInit(), bob);
    QVERIFY(responded.isError());
}

QTEST_MAIN(TestX3DH)
#include "test_x3dh.moc"
