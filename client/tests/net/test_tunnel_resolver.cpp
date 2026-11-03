#include "net/TunnelResolver.h"
#include "StubHttpServer.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::net;

class TestTunnelResolver : public QObject
{
    Q_OBJECT

private slots:
    void acceptsAllConnectionFormats();
    void rejectsBrokenValues();
    void readsTunnelAndFingerprintFromReadme();
    void resolvesConfigThenCacheThenDefault();
    void brokenConfigFlagsManualEntry();
    void fetchUpdatesEndpointFromLocalServer();
    void fetchFailureKeepsCachedEndpoint();
};

namespace {

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString readmeWith(const QString &tunnel, const QString &fingerprint = {})
{
    QString readme = QStringLiteral("# MINI Messenger\n\nSome text.\n\nCURRENT TUNNEL: %1\n")
                         .arg(tunnel);
    if (!fingerprint.isEmpty()) {
        readme += QStringLiteral("CURRENT FINGERPRINT: %1\n").arg(fingerprint);
    }
    return readme;
}

} // namespace

void TestTunnelResolver::acceptsAllConnectionFormats()
{
    const auto plain = TunnelResolver::parseEndpoint(QStringLiteral("bore.pub:54321"));
    QVERIFY2(plain.isOk(), qPrintable(plain.error()));
    QCOMPARE(plain.value().host(), QStringLiteral("bore.pub"));
    QCOMPARE(plain.value().port(), quint16(54321));
    QCOMPARE(plain.value().scheme(), QStringLiteral("https"));

    const auto local = TunnelResolver::parseEndpoint(QStringLiteral("127.0.0.1:8765"));
    QVERIFY(local.isOk());
    QCOMPARE(local.value().scheme(), QStringLiteral("http"));

    const auto http = TunnelResolver::parseEndpoint(QStringLiteral("http://example.org:8080"));
    QVERIFY(http.isOk());
    QCOMPARE(http.value().scheme(), QStringLiteral("http"));

    const auto https = TunnelResolver::parseEndpoint(QStringLiteral("https://example.org:443"));
    QVERIFY(https.isOk());
    QCOMPARE(https.value().scheme(), QStringLiteral("https"));

    const QString withBom = QString(QChar(0xFEFF)) + QStringLiteral("  host.example:1234 \r\n");
    const auto bom = TunnelResolver::parseEndpoint(withBom);
    QVERIFY(bom.isOk());
    QCOMPARE(bom.value().host(), QStringLiteral("host.example"));

    const auto ipv6 = TunnelResolver::parseEndpoint(QStringLiteral("https://[::1]:9443"));
    QVERIFY(ipv6.isOk());
    QCOMPARE(ipv6.value().host(), QStringLiteral("[::1]"));
    QCOMPARE(ipv6.value().port(), quint16(9443));
}

void TestTunnelResolver::rejectsBrokenValues()
{
    const QStringList broken = {
        QString(),
        QStringLiteral("   "),
        QStringLiteral("nohost"),
        QStringLiteral("host:0"),
        QStringLiteral("host:65536"),
        QStringLiteral("host:abc"),
        QStringLiteral("ftp://host:21"),
        QStringLiteral("http://host:80/path"),
        QStringLiteral("http://:80"),
        QStringLiteral("host:80 extra"),
        QStringLiteral("http://host:80?x=1"),
        QStringLiteral("user@host:80"),
    };
    for (const QString &value : broken) {
        QVERIFY2(TunnelResolver::parseEndpoint(value).isError(), qPrintable(value));
    }
}

void TestTunnelResolver::readsTunnelAndFingerprintFromReadme()
{
    const QString fingerprint = QStringLiteral("AB").repeated(32);
    QString pin;
    const auto parsed = TunnelResolver::parseReadme(
        readmeWith(QStringLiteral("bore.pub:45678"), fingerprint), &pin);
    QVERIFY2(parsed.isOk(), qPrintable(parsed.error()));
    QCOMPARE(parsed.value().host(), QStringLiteral("bore.pub"));
    QCOMPARE(parsed.value().port(), quint16(45678));
    QCOMPARE(pin, fingerprint);

    QString noPin;
    const auto withoutPin =
        TunnelResolver::parseReadme(readmeWith(QStringLiteral("bore.pub:1")), &noPin);
    QVERIFY(withoutPin.isOk());
    QVERIFY(noPin.isEmpty());

    const auto missing = TunnelResolver::parseReadme(QStringLiteral("# README\nnothing here"));
    QVERIFY(missing.isError());
    QCOMPARE(missing.error(), QStringLiteral("readme_has_no_tunnel"));

    const auto brokenTunnel =
        TunnelResolver::parseReadme(readmeWith(QStringLiteral("not-a-host")));
    QVERIFY(brokenTunnel.isError());
}

void TestTunnelResolver::resolvesConfigThenCacheThenDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath(QStringLiteral("connect.ip"));
    const QString cache = dir.filePath(QStringLiteral("connect.cache"));
    QNetworkAccessManager network;
    TunnelResolver resolver(config, cache, QUrl(), &network);

    auto resolved = resolver.resolve();
    QVERIFY(resolved.isOk());
    QCOMPARE(resolved.value().host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(resolved.value().port(), quint16(8765));
    QVERIFY(resolver.source() == TunnelResolver::Source::Default);
    QVERIFY(!resolver.needsManualEntry());

    QFile configFile(config);
    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    configFile.write("http://127.0.0.1:7000\n");
    configFile.close();

    resolved = resolver.resolve();
    QVERIFY(resolved.isOk());
    QCOMPARE(resolved.value().port(), quint16(7000));
    QVERIFY(resolver.source() == TunnelResolver::Source::Config);
    QVERIFY(!resolver.needsManualEntry());
}

void TestTunnelResolver::brokenConfigFlagsManualEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath(QStringLiteral("connect.ip"));
    const QString cache = dir.filePath(QStringLiteral("connect.cache"));

    {
        QFile file(config);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("this is not an address");
    }
    {
        QFile file(cache);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("bore.pub:9999\n");
    }

    QNetworkAccessManager network;
    TunnelResolver resolver(config, cache, QUrl(), &network);
    const auto resolved = resolver.resolve();
    QVERIFY(resolved.isOk());
    QCOMPARE(resolved.value().host(), QStringLiteral("bore.pub"));
    QCOMPARE(resolved.value().scheme(), QStringLiteral("https"));
    QVERIFY(resolver.source() == TunnelResolver::Source::Cache);
    QVERIFY(resolver.needsManualEntry());
    QVERIFY(!resolver.lastError().isEmpty());
}

void TestTunnelResolver::fetchUpdatesEndpointFromLocalServer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath(QStringLiteral("connect.ip"));
    const QString cache = dir.filePath(QStringLiteral("connect.cache"));

    StubHttpServer server;
    QVERIFY(server.listen());
    QString tunnel = QStringLiteral("bore.pub:11111");
    server.setHandler([&tunnel](const StubHttpServer::Request &) {
        StubHttpServer::Response response;
        response.body = readmeWith(tunnel).toUtf8();
        return response;
    });

    QNetworkAccessManager network;
    TunnelResolver resolver(config, cache,
                            QUrl(server.baseUrl() + QStringLiteral("/README.md")), &network);
    QSignalSpy resolved(&resolver, &TunnelResolver::resolved);
    QSignalSpy failed(&resolver, &TunnelResolver::failed);

    resolver.fetchFromReadme();
    QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(readFile(config).contains(QStringLiteral("bore.pub:11111")));
    QVERIFY(readFile(cache).contains(QStringLiteral("bore.pub:11111")));

    tunnel = QStringLiteral("bore.pub:22222");
    resolver.fetchFromReadme();
    QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 2, 5000);
    QVERIFY(readFile(config).contains(QStringLiteral("bore.pub:22222")));

    const auto offline = resolver.resolve();
    QVERIFY(offline.isOk());
    QCOMPARE(offline.value().port(), quint16(22222));
}

void TestTunnelResolver::fetchFailureKeepsCachedEndpoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath(QStringLiteral("connect.ip"));
    const QString cache = dir.filePath(QStringLiteral("connect.cache"));

    {
        QFile file(config);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("https://cached.example:444\n");
    }

    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &) {
        StubHttpServer::Response response;
        response.status = 500;
        response.body = QByteArrayLiteral("{\"error\":{\"code\":\"server_error\"}}");
        return response;
    });

    QNetworkAccessManager network;
    TunnelResolver resolver(config, cache, QUrl(server.baseUrl()), &network);
    QSignalSpy failed(&resolver, &TunnelResolver::failed);

    resolver.fetchFromReadme();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);

    const auto offline = resolver.resolve();
    QVERIFY(offline.isOk());
    QCOMPARE(offline.value().host(), QStringLiteral("cached.example"));
    QVERIFY(resolver.source() == TunnelResolver::Source::Config);
}

QTEST_MAIN(TestTunnelResolver)
#include "test_tunnel_resolver.moc"
