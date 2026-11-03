#include "core/BuildInfo.h"
#include "services/DiagnosticsService.h"
#include "ui/DiagnosticsDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

void installProbe(DiagnosticsService &service, int pingMs = 12)
{
    service.setProbe([pingMs](const QString &, quint16,
                              std::function<void(DiagnosticsService::ProbeResult)> done) {
        DiagnosticsService::ProbeResult result;
        result.dnsOk = true;
        result.tcpOk = true;
        result.tlsOk = true;
        result.pingMs = pingMs;
        done(result);
    });
}

} // namespace

class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }
    void serviceReportsTenIndicators();
    void reportContainsNoSecrets();
    void dialogShowsRowsAndButtons();
    void dialogButtonsUpdateReportAndReconnect();
};

void TestDiagnostics::serviceReportsTenIndicators()
{
    DiagnosticsService service;
    installProbe(service);
    service.setEndpoint(QStringLiteral("127.0.0.1:8765"));
    service.setWsConnected(true, 2);
    service.setServerVersion(QStringLiteral("1.0.0"));
    service.setDatabase(true, 3);
    service.setCache(4096, 4, 120);

    service.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(service.snapshot().items.size() == 10, 3000);

    QStringList ids;
    for (const DiagnosticItem &item : service.snapshot().items) {
        ids.append(item.id);
    }
    const QStringList expected{QStringLiteral("version"), QStringLiteral("address"),
                               QStringLiteral("dns"),     QStringLiteral("tcp"),
                               QStringLiteral("tls"),     QStringLiteral("ws"),
                               QStringLiteral("ping"),    QStringLiteral("server"),
                               QStringLiteral("database"), QStringLiteral("cache")};
    QCOMPARE(ids, expected);
    QCOMPARE(service.snapshot().items.at(0).detail,
             QStringLiteral("%1 (Qt %2)").arg(BuildInfo::appVersion(), BuildInfo::qtVersion()));
    QCOMPARE(service.snapshot().items.at(1).detail, QStringLiteral("127.0.0.1:8765"));
    QVERIFY(service.snapshot().items.at(6).detail.contains(QStringLiteral("12 ms")));
}

void TestDiagnostics::reportContainsNoSecrets()
{
    DiagnosticsService service;
    installProbe(service);
    service.setEndpoint(
        QStringLiteral("https://alice:Secr3t@127.0.0.1:8765/api?token=abc123"));
    QCOMPARE(service.endpoint(), QStringLiteral("127.0.0.1:8765"));
    service.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(service.snapshot().items.size() == 10, 3000);

    const QString report = service.report();
    QVERIFY(!report.contains(QStringLiteral("Secr3t")));
    QVERIFY(!report.contains(QStringLiteral("abc123")));
    QVERIFY(!report.contains(QStringLiteral("token")));
    QVERIFY(!report.contains(QStringLiteral("alice")));
    QVERIFY(report.contains(QStringLiteral("version:")));
    QVERIFY(report.contains(QStringLiteral("cache:")));
}

void TestDiagnostics::dialogShowsRowsAndButtons()
{
    DiagnosticsService service;
    installProbe(service);
    service.setEndpoint(QStringLiteral("127.0.0.1:8765"));

    DiagnosticsDialog dialog(&service);
    QCOMPARE(dialog.table()->rowCount(), 10);
    QCOMPARE(dialog.table()->columnCount(), 2);
    QVERIFY(dialog.reconnectButton() != nullptr);
    QVERIFY(dialog.copyReportButton() != nullptr);
    QVERIFY(dialog.openLogsButton() != nullptr);
    QCOMPARE(dialog.reconnectButton()->objectName(), QStringLiteral("reconnectButton"));
    QCOMPARE(dialog.copyReportButton()->objectName(), QStringLiteral("copyReportButton"));
    QCOMPARE(dialog.openLogsButton()->objectName(), QStringLiteral("openLogsButton"));
}

void TestDiagnostics::dialogButtonsUpdateReportAndReconnect()
{
    DiagnosticsService service;
    installProbe(service, 7);
    service.setEndpoint(QStringLiteral("127.0.0.1:8765"));
    bool reconnected = false;
    service.setReconnect([&reconnected] { reconnected = true; });

    DiagnosticsDialog dialog(&service);
    QString openedPath;
    dialog.setUrlOpener([&openedPath](const QString &path) {
        openedPath = path;
        return true;
    });
    dialog.setLogDirectory(QStringLiteral("C:/logs/mini"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog.table()->rowCount() == 10, 3000);

    dialog.openLogsButton()->click();
    QCOMPARE(openedPath, QStringLiteral("C:/logs/mini"));

    dialog.reconnectButton()->click();
    QVERIFY(reconnected);

    dialog.copyReportButton()->click();
    QCOMPARE(QApplication::clipboard()->text(), service.report());
}

QTEST_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
