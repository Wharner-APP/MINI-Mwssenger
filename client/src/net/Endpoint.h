#pragma once

#include <QString>
#include <QUrl>

namespace mini::net {

// A validated server address. The scheme is "http" or "https"; the websocket
// URL is derived from it (http -> ws, https -> wss).
class Endpoint
{
public:
    Endpoint() = default;
    Endpoint(QString scheme, QString host, quint16 port);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString scheme() const { return m_scheme; }
    [[nodiscard]] QString host() const { return m_host; }
    [[nodiscard]] quint16 port() const { return m_port; }
    [[nodiscard]] bool isSecure() const { return m_scheme == QStringLiteral("https"); }

    [[nodiscard]] QUrl httpUrl(const QString &pathAndQuery = QString()) const;
    [[nodiscard]] QUrl wsUrl(const QString &pathAndQuery = QString()) const;

    // "host:port", with IPv6 hosts wrapped in brackets.
    [[nodiscard]] QString hostPort() const;
    // "scheme://host:port".
    [[nodiscard]] QString toString() const;

    bool operator==(const Endpoint &other) const;
    bool operator!=(const Endpoint &other) const { return !(*this == other); }

private:
    QString m_scheme;
    QString m_host;
    quint16 m_port = 0;
};

} // namespace mini::net
