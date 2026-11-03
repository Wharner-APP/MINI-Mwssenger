#include "crypto/IdentityKeys.h"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <sodium.h>

using mini::Result;
using mini::crypto::Ed25519KeyPair;
using mini::crypto::IdentityKeys;
using mini::crypto::IdentityState;
using mini::crypto::OneTimePrekey;
using mini::crypto::PrekeyBundle;

namespace {

QByteArray keyWrap(char fill)
{
    return QByteArray(32, fill);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool verifyPrekeySignature(const IdentityState &state)
{
    return crypto_sign_verify_detached(
               reinterpret_cast<const unsigned char *>(state.signedPrekey.signature.constData()),
               reinterpret_cast<const unsigned char *>(state.signedPrekey.pair.publicKey.constData()),
               static_cast<unsigned long long>(state.signedPrekey.pair.publicKey.size()),
               reinterpret_cast<const unsigned char *>(state.identity.publicKey.constData()))
        == 0;
}

} // namespace

class TestIdentityKeys : public QObject
{
    Q_OBJECT

private slots:
    void generatesIdentitySignedPrekeyAndTwentyOneTimePrekeys();
    void bundleCarriesVerifiableSignature();
    void keysBinRoundTrips();
    void keysBinNeedsTheWrapKey();
    void missingFileIsNotSilentlyReplaced();
    void corruptedEntryIsRejected();
    void rotateReplacesStaleSignedPrekeyAndRefillsPool();
    void consumeRemovesOneTimePrekey();
    void wipeZeroesPrivateKeysButNotCopies();
};

void TestIdentityKeys::generatesIdentitySignedPrekeyAndTwentyOneTimePrekeys()
{
    const Result<IdentityState> generated = IdentityKeys::generate(1000);
    QVERIFY2(generated.isOk(), qPrintable(generated.error()));
    const IdentityState &state = generated.value();

    QVERIFY(state.isValid());
    QCOMPARE(state.identity.publicKey.size(), 32);
    QCOMPARE(state.identity.privateKey.size(), 64);
    QVERIFY(state.signedPrekey.id != 0);
    QCOMPARE(state.signedPrekey.signature.size(), 64);
    QCOMPARE(state.signedPrekey.createdAtMs, qint64(1000));
    QCOMPARE(state.oneTimePrekeys.size(), IdentityKeys::kOneTimePrekeyCount);

    QSet<quint32> ids;
    ids.insert(state.signedPrekey.id);
    for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
        QVERIFY(prekey.isValid());
        QVERIFY(!ids.contains(prekey.id));
        ids.insert(prekey.id);
    }
    QCOMPARE(ids.size(), 1 + IdentityKeys::kOneTimePrekeyCount);
}

void TestIdentityKeys::bundleCarriesVerifiableSignature()
{
    const IdentityState state = IdentityKeys::generate(5).value();
    const Result<PrekeyBundle> bundle = IdentityKeys::publicBundle(state);
    QVERIFY2(bundle.isOk(), qPrintable(bundle.error()));

    QCOMPARE(bundle.value().identityKey, state.identity.publicKey);
    QCOMPARE(bundle.value().signedPrekeyId, state.signedPrekey.id);
    QCOMPARE(bundle.value().signedPrekey, state.signedPrekey.pair.publicKey);
    QCOMPARE(bundle.value().oneTimePrekeyId, state.oneTimePrekeys.first().id);
    QCOMPARE(bundle.value().oneTimePrekey, state.oneTimePrekeys.first().pair.publicKey);

    QCOMPARE(crypto_sign_verify_detached(
                 reinterpret_cast<const unsigned char *>(
                     bundle.value().signedPrekeySignature.constData()),
                 reinterpret_cast<const unsigned char *>(bundle.value().signedPrekey.constData()),
                 static_cast<unsigned long long>(bundle.value().signedPrekey.size()),
                 reinterpret_cast<const unsigned char *>(bundle.value().identityKey.constData())),
             0);

    const Result<PrekeyBundle> withoutOneTime =
        IdentityKeys::publicBundle(state, 0xffffu);
    QVERIFY(withoutOneTime.isError());
    QCOMPARE(withoutOneTime.error(), QStringLiteral("crypto.keys_prekey_missing"));
}

void TestIdentityKeys::keysBinRoundTrips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keys.bin"));
    const QByteArray wrap = keyWrap('\x33');
    const QString deviceId = QStringLiteral("device-1");

    const IdentityState original = IdentityKeys::generate(42).value();
    const Result<int> saved = IdentityKeys::save(original, path, wrap, deviceId);
    QVERIFY2(saved.isOk(), qPrintable(saved.error()));
    QVERIFY(saved.value() > 0);
    QVERIFY(QFile::exists(path));

    const Result<IdentityState> loaded = IdentityKeys::load(path, wrap, deviceId);
    QVERIFY2(loaded.isOk(), qPrintable(loaded.error()));
    const IdentityState &restored = loaded.value();

    QCOMPARE(restored.identity.publicKey, original.identity.publicKey);
    QCOMPARE(restored.identity.privateKey, original.identity.privateKey);
    QCOMPARE(restored.signedPrekey.id, original.signedPrekey.id);
    QCOMPARE(restored.signedPrekey.createdAtMs, original.signedPrekey.createdAtMs);
    QCOMPARE(restored.signedPrekey.pair.publicKey, original.signedPrekey.pair.publicKey);
    QCOMPARE(restored.signedPrekey.pair.privateKey, original.signedPrekey.pair.privateKey);
    QCOMPARE(restored.signedPrekey.signature, original.signedPrekey.signature);
    QCOMPARE(restored.oneTimePrekeys.size(), original.oneTimePrekeys.size());
    for (int i = 0; i < original.oneTimePrekeys.size(); ++i) {
        QCOMPARE(restored.oneTimePrekeys.at(i).id, original.oneTimePrekeys.at(i).id);
        QCOMPARE(restored.oneTimePrekeys.at(i).pair.publicKey,
                 original.oneTimePrekeys.at(i).pair.publicKey);
        QCOMPARE(restored.oneTimePrekeys.at(i).pair.privateKey,
                 original.oneTimePrekeys.at(i).pair.privateKey);
    }
    QVERIFY(restored.isValid());
}

void TestIdentityKeys::keysBinNeedsTheWrapKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keys.bin"));
    const QString deviceId = QStringLiteral("device-1");

    QVERIFY(IdentityKeys::save(IdentityKeys::generate(1).value(), path, keyWrap('\x11'),
                               deviceId)
                .isOk());
    const QByteArray before = readAll(path);
    QVERIFY(!before.isEmpty());

    const Result<IdentityState> wrong =
        IdentityKeys::load(path, keyWrap('\x22'), deviceId);
    QVERIFY(wrong.isError());
    QCOMPARE(wrong.error(), QStringLiteral("crypto.keys_unreadable"));

    // A failed open must never overwrite the original secret file.
    QCOMPARE(readAll(path), before);

    const Result<IdentityState> good =
        IdentityKeys::load(path, keyWrap('\x11'), deviceId);
    QVERIFY2(good.isOk(), qPrintable(good.error()));
}

void TestIdentityKeys::missingFileIsNotSilentlyReplaced()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keys.bin"));
    const QByteArray wrap = keyWrap('\x44');
    const QString deviceId = QStringLiteral("device-1");

    const Result<IdentityState> missing = IdentityKeys::load(path, wrap, deviceId);
    QVERIFY(missing.isError());
    QCOMPARE(missing.error(), QStringLiteral("crypto.keys_missing"));
    QVERIFY(!QFile::exists(path));

    const Result<IdentityState> created = IdentityKeys::loadOrCreate(path, wrap, deviceId, 77);
    QVERIFY2(created.isOk(), qPrintable(created.error()));
    QVERIFY(QFile::exists(path));

    const Result<IdentityState> reopened = IdentityKeys::loadOrCreate(path, wrap, deviceId);
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().identity.publicKey, created.value().identity.publicKey);
}

void TestIdentityKeys::corruptedEntryIsRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keys.bin"));
    const QByteArray wrap = keyWrap('\x55');
    const QString deviceId = QStringLiteral("device-1");
    QVERIFY(IdentityKeys::save(IdentityKeys::generate(2).value(), path, wrap, deviceId).isOk());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray contents = file.readAll();
    QVERIFY(contents.size() > 16);
    contents[contents.size() - 1] = static_cast<char>(contents.at(contents.size() - 1) ^ 0x01);
    file.seek(0);
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    const Result<IdentityState> broken = IdentityKeys::load(path, wrap, deviceId);
    QVERIFY(broken.isError());
    QCOMPARE(broken.error(), QStringLiteral("crypto.keys_unreadable"));
}

void TestIdentityKeys::rotateReplacesStaleSignedPrekeyAndRefillsPool()
{
    IdentityState state = IdentityKeys::generate(1000).value();
    const quint32 firstSignedId = state.signedPrekey.id;
    const quint32 firstOneTimeId = state.oneTimePrekeys.first().id;

    const IdentityState rotated =
        IdentityKeys::rotatePrekeys(state, 1000 + IdentityKeys::kSignedPrekeyLifetimeMs + 1)
            .value();
    QVERIFY(rotated.signedPrekey.id != firstSignedId);
    QCOMPARE(rotated.oneTimePrekeys.size(), IdentityKeys::kOneTimePrekeyCount);
    QCOMPARE(rotated.oneTimePrekeys.first().id, firstOneTimeId);
    QVERIFY(verifyPrekeySignature(rotated));

    IdentityState consumed = rotated;
    QVERIFY(IdentityKeys::consumeOneTimePrekey(consumed, consumed.oneTimePrekeys.first().id)
                .isOk());
    QCOMPARE(consumed.oneTimePrekeys.size(), IdentityKeys::kOneTimePrekeyCount - 1);

    const IdentityState refilled =
        IdentityKeys::rotatePrekeys(consumed, 1000 + IdentityKeys::kSignedPrekeyLifetimeMs + 2)
            .value();
    QCOMPARE(refilled.oneTimePrekeys.size(), IdentityKeys::kOneTimePrekeyCount);
    QCOMPARE(refilled.signedPrekey.id, rotated.signedPrekey.id);
}

void TestIdentityKeys::consumeRemovesOneTimePrekey()
{
    IdentityState state = IdentityKeys::generate(0).value();
    const quint32 id = state.oneTimePrekeys.at(3).id;
    const Result<OneTimePrekey> consumed = IdentityKeys::consumeOneTimePrekey(state, id);
    QVERIFY2(consumed.isOk(), qPrintable(consumed.error()));
    QCOMPARE(consumed.value().id, id);
    QCOMPARE(state.oneTimePrekeys.size(), IdentityKeys::kOneTimePrekeyCount - 1);
    QVERIFY(IdentityKeys::consumeOneTimePrekey(state, id).isError());
}

void TestIdentityKeys::wipeZeroesPrivateKeysButNotCopies()
{
    IdentityState state = IdentityKeys::generate(9).value();
    IdentityState copy = state;
    QCOMPARE(copy.identity.privateKey, state.identity.privateKey);

    state.wipe();
    QCOMPARE(state.identity.privateKey.size(), 64);
    for (const char byte : state.identity.privateKey) {
        QCOMPARE(byte, '\0');
    }
    for (const char byte : state.signedPrekey.pair.privateKey) {
        QCOMPARE(byte, '\0');
    }
    for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
        for (const char byte : prekey.pair.privateKey) {
            QCOMPARE(byte, '\0');
        }
    }

    // The deep copy must keep its own bytes: QByteArray sharing must not let
    // a wiped temporary corrupt a live key set.
    QVERIFY(copy.identity.privateKey != QByteArray(64, '\0'));
}

QTEST_MAIN(TestIdentityKeys)
#include "test_identity_keys.moc"
