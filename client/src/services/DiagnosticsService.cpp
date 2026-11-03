#include "services/DiagnosticsService.h"

#include "core/BuildInfo.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QList>
#include <QSslError>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace mini::services {

namespace {

QString sanitizeEndpoint(const QString &raw, QString *host, quint16 *port)
{
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    const bool hasScheme = text.contains(QStringLiteral("://"));
    const QUrl url = hasScheme ? QUrl(text) : QUrl(QStringLiteral("https://") + text);
    const QString parsedHost = url.host();
    if (parsedHost.isEmpty()) {
        return {};
    }
    const int parsedPort = url.port(hasScheme && url.scheme() == QLatin1String("http") ? 80 : 443);
    if (port) {
        *port = static_cast<quint16>(qBound(1, parsedPort, 65535));
    }
    if (host) {
        *host = parsedHost;
    }
    const QString displayHost = parsedHost.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(parsedHost)
        : parsedHost;
    return QStringLiteral("%1:%2").arg(displayHost).arg(parsedPort);
}

// DNS -> TCP -> TLS probe with a hard 4 s budget. Only reachability is
// measured; certificate errors are ignored on purpose because the indicator
// answers "did a TLS session start", not "is the pin trusted".
class ProbeTask : public QObject
{
public:
    using Done = std::function<void(DiagnosticsService::ProbeResult)>;

    ProbeTask(QString host, quint16 port, Done done)
        : m_host(std::move(host)), m_port(port), m_done(std::move(done))
    {
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(4000);
        QObject::connect(&m_timeout, &QTimer::timeout, this, [this] { finish(); });
    }

    void start()
    {
        m_timeout.start();
        QHostInfo::lookupHost(m_host, this, [this](const QHostInfo &info) {
            m_result.dnsOk = info.error() == QHostInfo::NoError && !info.addresses().isEmpty();
            if (!m_result.dnsOk) {
                finish();
                return;
            }
            startTcp();
        });
    }

private:
    void startTcp()
    {
        auto *socket = new QTcpSocket(this);
        QObject::connect(socket, &QTcpSocket::connected, this, [this, socket] {
            m_result.tcpOk = true;
            m_result.pingMs = static_cast<int>(m_clock.elapsed());
            socket->disconnectFromHost();
            startTls();
        });
        QObject::connect(socket, &QTcpSocket::errorOccurred, this,
                         [this](QAbstractSocket::SocketError) {
                             if (!m_result.tcpOk) {
                                 finish();
                             }
                         });
        m_clock.start();
        socket->connectToHost(m_host, m_port);
    }

    void startTls()
    {
        auto *socket = new QSslSocket(this);
        QObject::connect(socket, &QSslSocket::encrypted, this, [this] {
            m_result.tlsOk = true;
            finish();
        });
        QObject::connect(socket,
                         QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors), this,
                         [socket](const QList<QSslError> &) { socket->ignoreSslErrors(); });
        QObject::connect(socket, &QAbstractSocket::errorOccurred, this,
                         [this](QAbstractSocket::SocketError) { finish(); });
        socket->connectToHostEncrypted(m_host, m_port);
    }

    void finish()
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_timeout.stop();
        if (m_done) {
            m_done(m_result);
        }
        deleteLater();
    }

    QString m_host;
    quint16 m_port = 0;
    Done m_done;
    DiagnosticsService::ProbeResult m_result;
    QTimer m_timeout;
    QElapsedTimer m_clock;
    bool m_finished = false;
};

DiagnosticsService::Probe defaultProbe()
{
    return [](const QString &host, quint16 port,
              std::function<void(DiagnosticsService::ProbeResult)> done) {
        auto *task = new ProbeTask(host, port, std::move(done));
        task->start();
    };
}

QString statusText(DiagnosticStatus status)
{
    switch (status) {
    case DiagnosticStatus::Ok:
        return QStringLiteral("ok");
    case DiagnosticStatus::Fail:
        return QStringLiteral("fail");
    case DiagnosticStatus::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace

DiagnosticsService::DiagnosticsService(QObject *parent) : QObject(parent) {}

void DiagnosticsService::setEndpoint(const QString &endpoint)
{
    QString host;
    quint16 port = 0;
    const QString sanitized = sanitizeEndpoint(endpoint, &host, &port);
    m_endpoint = sanitized;
    m_host = host;
    m_port = port;
}

void DiagnosticsService::setWsConnected(bool connected, int attempts)
{
    m_wsConnected = connected;
    m_wsAttempts = qMax(0, attempts);
}

void DiagnosticsService::setServerVersion(const QString &version)
{
    m_serverVersion = version;
}

void DiagnosticsService::setDatabase(bool open, int schemaVersion)
{
    m_databaseOpen = open;
    m_schemaVersion = qMax(0, schemaVersion);
}

void DiagnosticsService::setCache(qint64 bytes, int chats, int messages)
{
    m_cacheBytes = qMax<qint64>(0, bytes);
    m_cacheChats = qMax(0, chats);
    m_cacheMessages = qMax(0, messages);
}

void DiagnosticsService::reconnect()
{
    if (m_reconnect) {
        m_reconnect();
    }
    refresh();
}

void DiagnosticsService::refresh()
{
    if (m_refreshing) {
        return;
    }
    if (m_host.isEmpty()) {
        m_probeSeen = false;
        emit refreshed();
        return;
    }
    if (!m_probe) {
        m_probe = defaultProbe();
    }
    m_refreshing = true;
    const QString host = m_host;
    const quint16 port = m_port;
    m_probe(host, port, [this](ProbeResult result) {
        applyProbeResult(result);
        m_refreshing = false;
        emit refreshed();
    });
}

void DiagnosticsService::applyProbeResult(const ProbeResult &result)
{
    m_probeResult = result;
    m_probeSeen = true;
}

DiagnosticsSnapshot DiagnosticsService::snapshot() const
{
    DiagnosticsSnapshot snapshot;
    auto add = [&snapshot](const QString &id, DiagnosticStatus status,
                           const QString &detail = QString()) {
        snapshot.items.append(DiagnosticItem{id, status, detail});
    };

    add(QStringLiteral("version"), DiagnosticStatus::Ok,
        QStringLiteral("%1 (Qt %2)").arg(BuildInfo::appVersion(), BuildInfo::qtVersion()));
    add(QStringLiteral("address"),
        m_endpoint.isEmpty() ? DiagnosticStatus::Unknown : DiagnosticStatus::Ok, m_endpoint);
    add(QStringLiteral("dns"),
        !m_probeSeen ? DiagnosticStatus::Unknown
                     : (m_probeResult.dnsOk ? DiagnosticStatus::Ok : DiagnosticStatus::Fail),
        m_host);
    add(QStringLiteral("tcp"),
        !m_probeSeen ? DiagnosticStatus::Unknown
                     : (m_probeResult.tcpOk ? DiagnosticStatus::Ok : DiagnosticStatus::Fail),
        m_port > 0 ? QString::number(m_port) : QString());
    add(QStringLiteral("tls"),
        !m_probeSeen ? DiagnosticStatus::Unknown
                     : (m_probeResult.tlsOk ? DiagnosticStatus::Ok : DiagnosticStatus::Fail),
        QString());
    add(QStringLiteral("ws"),
        m_wsConnected ? DiagnosticStatus::Ok : DiagnosticStatus::Fail,
        m_wsConnected ? QString() : QStringLiteral("attempts: %1").arg(m_wsAttempts));
    add(QStringLiteral("ping"),
        m_probeSeen && m_probeResult.pingMs >= 0 ? DiagnosticStatus::Ok
                                                 : DiagnosticStatus::Unknown,
        m_probeSeen && m_probeResult.pingMs >= 0
            ? QStringLiteral("%1 ms").arg(m_probeResult.pingMs)
            : QString());
    add(QStringLiteral("server"),
        m_serverVersion.isEmpty() ? DiagnosticStatus::Unknown : DiagnosticStatus::Ok,
        m_serverVersion);
    add(QStringLiteral("database"),
        m_databaseOpen ? DiagnosticStatus::Ok : DiagnosticStatus::Fail,
        QStringLiteral("schema %1").arg(m_schemaVersion));
    add(QStringLiteral("cache"), DiagnosticStatus::Ok,
        QStringLiteral("%1 chats · %2 messages · %3 KB")
            .arg(m_cacheChats)
            .arg(m_cacheMessages)
            .arg(m_cacheBytes / 1024));
    return snapshot;
}

QString DiagnosticsService::report() const
{
    QStringList lines;
    lines.append(QStringLiteral("MINI Messenger diagnostics"));
    lines.append(QStringLiteral("generated: %1")
                     .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    for (const DiagnosticItem &item : snapshot().items) {
        if (item.detail.isEmpty()) {
            lines.append(QStringLiteral("%1: %2").arg(item.id, statusText(item.status)));
        } else {
            lines.append(QStringLiteral("%1: %2 (%3)")
                             .arg(item.id, statusText(item.status), item.detail));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace mini::services
