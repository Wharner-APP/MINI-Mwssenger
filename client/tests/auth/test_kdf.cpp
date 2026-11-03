#include "crypto/Kdf.h"

#include <QtTest>

using mini::Result;
using mini::crypto::FileKeys;
using mini::crypto::Kdf;

namespace {

QByteArray fromHex(const QByteArray &hex)
{
    return QByteArray::fromHex(hex);
}

} // namespace

class TestKdf : public QObject
{
    Q_OBJECT

private slots:
    void hkdfMatchesRfc5869Vector();
    void passwordKeyMatchesArgon2idReference();
    void fileKeysMatchReference();
    void dbAndWrapBranchesDiffer();
    void shortSaltIsRejected();
};

void TestKdf::hkdfMatchesRfc5869Vector()
{
    const QByteArray ikm = fromHex(QByteArrayLiteral("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"));
    const QByteArray salt = fromHex(QByteArrayLiteral("000102030405060708090a0b0c"));
    const QByteArray info = fromHex(QByteArrayLiteral("f0f1f2f3f4f5f6f7f8f9"));
    const QByteArray okm = Kdf::hkdfSha256(ikm, salt, info, 42);
    QCOMPARE(okm.toHex(),
             QByteArrayLiteral("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                               "34007208d5b887185865"));
}

void TestKdf::passwordKeyMatchesArgon2idReference()
{
    const QByteArray salt = fromHex(QByteArrayLiteral("000102030405060708090a0b0c0d0e0f"));
    const Result<QByteArray> key =
        Kdf::derivePasswordKey(QStringLiteral("correct horse battery staple"), salt);
    QVERIFY2(key.isOk(), qPrintable(key.error()));
    QCOMPARE(key.value().toHex(),
             QByteArrayLiteral("0d1a3c6523c8f06e4e0af9c515aa5b5448cfebd6838f2d52c3d8b6ef8ddc3c2e"));
}

void TestKdf::fileKeysMatchReference()
{
    const QByteArray salt = fromHex(QByteArrayLiteral("101112131415161718191a1b1c1d1e1f"));
    const Result<FileKeys> keys =
        Kdf::deriveFileKeys(QStringLiteral("test normalized"), salt);
    QVERIFY2(keys.isOk(), qPrintable(keys.error()));
    QCOMPARE(keys.value().kfile.toHex(),
             QByteArrayLiteral("604f699f4fe95ff04f6cf702b826a513f7c677c059ef6475a9a835a4afeb871d"));
    QCOMPARE(keys.value().kauth.toHex(),
             QByteArrayLiteral("7f393568fc136bdb89a7199883dd80d335e4a725b53e2bc0e0f0289022eb92c7"));
    QCOMPARE(keys.value().kenc.toHex(),
             QByteArrayLiteral("0dff0c7a4281425edcd0e6690187211d72422099d49764044115c5d5fca16f61"));
    QCOMPARE(keys.value().kmac.toHex(),
             QByteArrayLiteral("375c12a480ffdabe0a5c6ee0ca2beb821c8000fc0d8b7d489b73693ee532dced"));
}

void TestKdf::dbAndWrapBranchesDiffer()
{
    const QByteArray kenc = QByteArray(32, '\0');
    const Result<QByteArray> db = Kdf::deriveDbKey(kenc);
    const Result<QByteArray> wrap = Kdf::deriveKeyWrap(kenc);
    QVERIFY(db.isOk());
    QVERIFY(wrap.isOk());
    QCOMPARE(db.value().toHex(),
             QByteArrayLiteral("2e95afe2f92c6e06954954f785f3512fd883fdb806a7ffb0de4f218b955ff7f9"));
    QVERIFY(db.value() != wrap.value());
}

void TestKdf::shortSaltIsRejected()
{
    const Result<QByteArray> key =
        Kdf::derivePasswordKey(QStringLiteral("Passw0rd!"), QByteArray(8, '\0'));
    QVERIFY(key.isError());
    QCOMPARE(key.error(), QStringLiteral("kdf.salt"));
}

QTEST_GUILESS_MAIN(TestKdf)
#include "test_kdf.moc"
