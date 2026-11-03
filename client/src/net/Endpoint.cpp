#include "net/Endpoint.h"

namespace mini::net {

namespace {

QString bareHost(const QString &host)
{
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
        return host.mid(1, host.size() - 2);
    }
    return host;
}

} // namespace

Endpoint::Endpoint(QString scheme, QString host, quint16 port)
    : m_scheme(scheme.trimmed().toLower())
    , m_host(host.trimmed())
    , m_port(port)
{
}

bool Endpoint::isValid() const
{
    return (m_scheme == QStringLiteral("http") || m_scheme == QStringLiteral("https"))
        && !m_host.isEmpty() && m_port != 0;
}

QUrl Endpoint::httpUrl(const QString &pathAndQuery) const
{
    QUrl url;
    url.setScheme(m_scheme);
    url.setHost(bareHost(m_host));
    url.setPort(m_port);
    if (!pathAndQuery.isEmpty()) {
        const int queryAt = pathAndQuery.indexOf(QLatin1Char('?'));
        if (queryAt >= 0) {
            url.setPath(pathAndQuery.left(queryAt));
            url.setQuery(pathAndQuery.mid(queryAt + 1), QUrl::StrictMode);
        } else {
            url.setPath(pathAndQuery);
        }
    }
    return url;
}

QUrl Endpoint::wsUrl(const QString &pathAndQuery) const
{
    QUrl url;
    url.setScheme(isSecure() ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setHost(bareHost(m_host));
    url.setPort(m_port);
    if (!pathAndQuery.isEmpty()) {
        const int queryAt = pathAndQuery.indexOf(QLatin1Char('?'));
        if (queryAt >= 0) {
            url.setPath(pathAndQuery.left(queryAt));
            url.setQuery(pathAndQuery.mid(queryAt + 1), QUrl::StrictMode);
        } else {
            url.setPath(pathAndQuery);
        }
    }
    return url;
}

QString Endpoint::hostPort() const
{
    const bool bracketed = m_host.startsWith(QLatin1Char('['))
        && m_host.endsWith(QLatin1Char(']'));
    const bool ipv6 = !bracketed && m_host.contains(QLatin1Char(':'));
    const QString host = bracketed ? m_host
        : ipv6              ? QStringLiteral("[%1]").arg(m_host)
                            : m_host;
    return host + QLatin1Char(':') + QString::number(m_port);
}

QString Endpoint::toString() const
{
    return m_scheme + QStringLiteral("://") + hostPort();
}

bool Endpoint::operator==(const Endpoint &other) const
{
    return m_scheme == other.m_scheme && m_host.compare(other.m_host, Qt::CaseInsensitive) == 0
        && m_port == other.m_port;
}

} // namespace mini::net
