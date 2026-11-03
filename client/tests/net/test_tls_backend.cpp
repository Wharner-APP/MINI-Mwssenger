// Regression: dist (and the build tree) must ship Qt's OpenSSL backend.
// windeployqt silently deploys only SChannel/cert-only when it cannot see
// libssl/libcrypto, and then every HTTPS request to the self-signed server
// fails. build.ps1 now deploys the backend and both libraries.

#include <QSslSocket>
#include <QtTest>

class TestTlsBackend : public QObject
{
    Q_OBJECT

private slots:
    void openSslBackendIsActive();
};

void TestTlsBackend::openSslBackendIsActive()
{
    QVERIFY2(QSslSocket::supportsSsl(), "Qt has no usable TLS backend");
    QVERIFY2(QSslSocket::availableBackends().contains(QStringLiteral("openssl")),
             qPrintable(QSslSocket::availableBackends().join(QStringLiteral(", "))));
    QCOMPARE(QSslSocket::activeBackend(), QStringLiteral("openssl"));
}

QTEST_MAIN(TestTlsBackend)
#include "test_tls_backend.moc"
