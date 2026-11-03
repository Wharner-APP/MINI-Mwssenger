#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::services {

enum class DiagnosticStatus { Ok, Fail, Unknown };

struct DiagnosticItem
{
    QString id;
    DiagnosticStatus status = DiagnosticStatus::Unknown;
    QString detail;
};

struct DiagnosticsSnapshot
{
    QVector<DiagnosticItem> items;
};

// Collects the ten indicators of the diagnostics screen (spec §4.7) and
// renders the plain-text report. It only ever sees host/port, versions and
// counters: tokens, sign.in material and message text never enter it.
class DiagnosticsService : public QObject
{
    Q_OBJECT

public:
    struct ProbeResult
    {
        bool dnsOk = false;
        bool tcpOk = false;
        bool tlsOk = false;
        int pingMs = -1;
    };
    using Probe = std::function<void(const QString &host, quint16 port,
                                     std::function<void(ProbeResult)> done)>;

    static constexpr int kItemCount = 10;

    explicit DiagnosticsService(QObject *parent = nullptr);

    // Accepts "host:port", "host" or any URL; only host and port are kept,
    // so user-info, paths and query strings can never reach the report.
    void setEndpoint(const QString &endpoint);
    [[nodiscard]] QString endpoint() const { return m_endpoint; }
    [[nodiscard]] QString host() const { return m_host; }
    [[nodiscard]] quint16 port() const { return m_port; }

    void setWsConnected(bool connected, int attempts = 0);
    void setServerVersion(const QString &version);
    void setDatabase(bool open, int schemaVersion);
    void setCache(qint64 bytes, int chats, int messages);

    void setProbe(Probe probe) { m_probe = std::move(probe); }
    void setReconnect(std::function<void()> reconnect) { m_reconnect = std::move(reconnect); }

    void reconnect();
    void refresh();

    [[nodiscard]] DiagnosticsSnapshot snapshot() const;
    [[nodiscard]] QString report() const;

signals:
    void refreshed();

private:
    void applyProbeResult(const ProbeResult &result);

    QString m_endpoint;
    QString m_host;
    quint16 m_port = 0;
    bool m_wsConnected = false;
    int m_wsAttempts = 0;
    QString m_serverVersion;
    bool m_databaseOpen = false;
    int m_schemaVersion = 0;
    qint64 m_cacheBytes = 0;
    int m_cacheChats = 0;
    int m_cacheMessages = 0;
    ProbeResult m_probeResult;
    bool m_probeSeen = false;
    bool m_refreshing = false;
    Probe m_probe;
    std::function<void()> m_reconnect;
};

} // namespace mini::services
