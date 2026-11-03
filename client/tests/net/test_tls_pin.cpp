#include "net/TlsPin.h"

#include <QCryptographicHash>
#include <QSignalSpy>
#include <QtTest>

using namespace mini::net;

namespace {

// Throwaway self-signed certificate generated for tests only. No private key
// is stored in the repository.
const char kTestCertificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDCTCCAfGgAwIBAgIUSG68L4xNOYE5/IWuD0HKAsDXyXQwDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJTUlOSSBUZXN0MB4XDTI2MTEwMjE1MDQ0MloXDTM2MTAz\n"
    "MDE1MDQ0MlowFDESMBAGA1UEAwwJTUlOSSBUZXN0MIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEA7eYS+lgROoJemfjFG7nKSXaZtXVW4n4m9Yd7UBtY6giC\n"
    "xIj56XDMZgGWSOMh3FDqO0me998yPHc5+7aDg0aTT+As2KF7DmDHBHiNNJ4NgKv+\n"
    "xr7770rNa8ddDodc52cRmLd/zJOgWUuj02m8xHGr81xXlwv/ThmLpmN6pUwQ4G8G\n"
    "g4DvYL1zchDgj0/HHCkQWXvZjUgSttYBb1Zl+SIU1rsyoIEchQt+faHmBrTOb5AD\n"
    "8jiRZ59c9BPJqHlwtSabyOquhZjaZc89H/dYedKGeCjGSPcBMVL8Sf5xcrxd2RXX\n"
    "/mh0kvzHnw5Xj70jZhwYXG1/NXMaWTovRnpB9IreQQIDAQABo1MwUTAdBgNVHQ4E\n"
    "FgQU7yar8TrZxCW7nGt+noumE5Kyw2MwHwYDVR0jBBgwFoAU7yar8TrZxCW7nGt+\n"
    "noumE5Kyw2MwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAZkDG\n"
    "0HN/a3QFCLcKnUEjVdlMXmF+k0D+yfjyPGPGGcsLiF4UDBEpLdYIeuCelr3TOS6h\n"
    "Za46fFBU+lyruy+mfWGTErelsQG1c1zqXw35aMwe99KV+5d5Pl4/tIcDU2Xqpp2r\n"
    "INOlizW0DDlfzncLjfzNdUw4JkZgP1yGKVgTLOan9z3m2lJJoskK8M7qTqPsc/Eo\n"
    "05eA3wPJ5mCeldSYkAKpxvOIuCpOFcZDeHxuyubChO/0GkIpKMtlfZ0TxFgTwEIr\n"
    "exau9g076idON9/pstOvyriKhVAqlcwGhe6JT6CcdQOBBrJRqZtUKJaG+ttV3DKG\n"
    "7vEgwmcQmhusVEfEqQ==\n"
    "-----END CERTIFICATE-----\n";

} // namespace

class TestTlsPin : public QObject
{
    Q_OBJECT

private slots:
    void normalizesFingerprints();
    void rejectsInvalidFingerprints();
    void fingerprintMatchesIndependentDigest();
    void comparesCertificateAgainstPin();
    void mismatchIsReportedAndCanBeAcceptedOnce();
    void warnsWhenPinIsMissingOrMalformed();
};

void TestTlsPin::normalizesFingerprints()
{
    const QString plain = QStringLiteral("ab").repeated(32);
    const QString coloned =
        QStringLiteral("AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:"
                       "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89");
    QCOMPARE(TlsPin::normalize(plain.toUpper()), plain.toUpper());
    QCOMPARE(TlsPin::normalize(coloned).size(), 64);
    QCOMPARE(TlsPin::normalize(QStringLiteral("sha256=") + plain), plain.toUpper());
    QVERIFY(TlsPin::isValid(plain));
    QVERIFY(TlsPin::isValid(coloned));
}

void TestTlsPin::rejectsInvalidFingerprints()
{
    QVERIFY(TlsPin::normalize(QString()).isEmpty());
    QVERIFY(TlsPin::normalize(QStringLiteral("ABC")).isEmpty());
    QVERIFY(TlsPin::normalize(QStringLiteral("zz").repeated(32)).isEmpty());
    QVERIFY(!TlsPin::isValid(QStringLiteral("not-a-fingerprint")));
}

void TestTlsPin::fingerprintMatchesIndependentDigest()
{
    const QSslCertificate certificate(QByteArray(kTestCertificate), QSsl::Pem);
    QVERIFY(!certificate.isNull());

    const QString expected = QString::fromLatin1(
        QCryptographicHash::hash(certificate.toDer(), QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
    QCOMPARE(TlsPin::fingerprint(certificate), expected);

    TlsPin pin(expected);
    QVERIFY(pin.check(certificate) == TlsPin::Outcome::Match);
    QVERIFY(pin.verify(certificate));
}

void TestTlsPin::comparesCertificateAgainstPin()
{
    const QSslCertificate certificate(QByteArray(kTestCertificate), QSsl::Pem);
    QVERIFY(!certificate.isNull());

    TlsPin empty;
    QVERIFY(empty.check(certificate) == TlsPin::Outcome::NotConfigured);
    QVERIFY(!empty.verify(certificate));

    TlsPin wrong(QStringLiteral("A").repeated(64));
    QVERIFY(wrong.check(certificate) == TlsPin::Outcome::Mismatch);
    QVERIFY(!wrong.verify(certificate));

    TlsPin configured(QStringLiteral("B").repeated(64));
    QVERIFY(configured.check(QSslCertificate()) == TlsPin::Outcome::Invalid);
}

void TestTlsPin::mismatchIsReportedAndCanBeAcceptedOnce()
{
    const QSslCertificate certificate(QByteArray(kTestCertificate), QSsl::Pem);
    QVERIFY(!certificate.isNull());

    TlsPin pin(QStringLiteral("A").repeated(64));
    QSignalSpy mismatch(&pin, &TlsPin::mismatchDetected);

    QVERIFY(!pin.shouldIgnore(certificate));
    QCOMPARE(mismatch.count(), 1);

    pin.acceptMismatchOnce();
    QVERIFY(pin.shouldIgnore(certificate));
    QVERIFY(!pin.shouldIgnore(certificate));
    QCOMPARE(mismatch.count(), 2);
}

void TestTlsPin::warnsWhenPinIsMissingOrMalformed()
{
    const QSslCertificate certificate(QByteArray(kTestCertificate), QSsl::Pem);
    QVERIFY(!certificate.isNull());

    TlsPin missing;
    QVERIFY(!missing.isConfigured());
    QVERIFY(!missing.verify(certificate));
    QSignalSpy missingWarning(&missing, &TlsPin::pinUnavailable);
    QVERIFY(!missing.shouldIgnore(certificate));
    QCOMPARE(missingWarning.count(), 1);
    QCOMPARE(missingWarning.at(0).at(0).toString(), QStringLiteral("not_configured"));
    QVERIFY(!missing.shouldIgnore(certificate));
    QCOMPARE(missingWarning.count(), 1);

    TlsPin malformed(QStringLiteral("not-a-fingerprint"));
    QVERIFY(!malformed.isConfigured());
    QVERIFY(malformed.isMalformed());
    QVERIFY(malformed.check(certificate) == TlsPin::Outcome::Invalid);
    QVERIFY(!malformed.verify(certificate));
    QSignalSpy malformedWarning(&malformed, &TlsPin::pinUnavailable);
    QVERIFY(!malformed.shouldIgnore(certificate));
    QCOMPARE(malformedWarning.count(), 1);
    QCOMPARE(malformedWarning.at(0).at(0).toString(),
             QStringLiteral("invalid_fingerprint"));
}

QTEST_MAIN(TestTlsPin)
#include "test_tls_pin.moc"
