#include "net/TunnelResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace mini::net {

namespace {

constexpr int kTransferTimeoutMs = 15000;

QString stripBom(QString text)
{
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    return text;
}

bool isLocalHost(const QString &host)
{
    const QString lower = host.toLower();
    return lower == QStringLiteral("localhost") || lower == QStringLiteral("::1")
        || lower.startsWith(QStringLiteral("127."));
}

bool isValidHost(const QString &host)
{
    if (host.isEmpty() || host.size() > 253) {
        return false;
    }

    if (host.startsWith(QLatin1Char('[')) || host.endsWith(QLatin1Char(']'))) {
        if (!host.startsWith(QLatin1Char('[')) || !host.endsWith(QLatin1Char(']'))) {
            return false;
        }
        const QString inner = host.mid(1, host.size() - 2);
        static const QRegularExpression ipv6(QStringLiteral("^[0-9A-Fa-f:.]+$"));
        return inner.contains(QLatin1Char(':')) && ipv6.match(inner).hasMatch();
    }

    static const QRegularExpression label(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
                       "(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));
    return label.match(host).hasMatch();
}

bool writeAtomic(const QString &path, const QByteArray &data)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace

TunnelResolver::TunnelResolver(QString configPath, QString cachePath, QUrl readmeUrl,
                               QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_configPath(std::move(configPath))
    , m_cachePath(std::move(cachePath))
    , m_readmeUrl(std::move(readmeUrl))
    , m_network(network)
{
}

QUrl TunnelResolver::defaultReadmeUrl()
{
    return QUrl(QStringLiteral(
        "https://raw.githubusercontent.com/Wharner-APP/MINI-Mwssenger/main/README.md"));
}

Result<Endpoint> TunnelResolver::parseEndpoint(const QString &text)
{
    QString value = stripBom(text).trimmed();
    if (value.isEmpty()) {
        return Result<Endpoint>::error(QStringLiteral("empty"));
    }
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))
        || value.contains(QLatin1Char(' ')) || value.contains(QLatin1Char('\t'))) {
        return Result<Endpoint>::error(QStringLiteral("whitespace"));
    }
    if (value.contains(QLatin1Char('?')) || value.contains(QLatin1Char('#'))
        || value.contains(QLatin1Char('@'))) {
        return Result<Endpoint>::error(QStringLiteral("invalid_characters"));
    }
    while (value.endsWith(QLatin1Char('/'))) {
        value.chop(1);
    }

    QString scheme;
    const int schemeEnd = value.indexOf(QStringLiteral("://"));
    if (schemeEnd >= 0) {
        scheme = value.left(schemeEnd).toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
            return Result<Endpoint>::error(QStringLiteral("invalid_scheme"));
        }
        value = value.mid(schemeEnd + 3);
    }
    if (value.contains(QLatin1Char('/'))) {
        return Result<Endpoint>::error(QStringLiteral("path_not_allowed"));
    }

    QString host;
    QString portText;
    if (value.startsWith(QLatin1Char('['))) {
        const int close = value.indexOf(QLatin1Char(']'));
        if (close < 0 || close + 1 >= value.size() || value.at(close + 1) != QLatin1Char(':')) {
            return Result<Endpoint>::error(QStringLiteral("missing_port"));
        }
        host = value.left(close + 1);
        portText = value.mid(close + 2);
    } else {
        const int colon = value.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0 || colon == value.size() - 1) {
            return Result<Endpoint>::error(QStringLiteral("missing_port"));
        }
        host = value.left(colon);
        portText = value.mid(colon + 1);
    }

    if (!isValidHost(host)) {
        return Result<Endpoint>::error(QStringLiteral("invalid_host"));
    }

    bool portOk = false;
    const int port = portText.toInt(&portOk);
    if (!portOk || port < 1 || port > 65535) {
        return Result<Endpoint>::error(QStringLiteral("invalid_port"));
    }

    if (scheme.isEmpty()) {
        scheme = isLocalHost(host) ? QStringLiteral("http") : QStringLiteral("https");
    }

    return Result<Endpoint>::ok(Endpoint(scheme, host, static_cast<quint16>(port)));
}

Result<Endpoint> TunnelResolver::parseReadme(const QString &readme, QString *fingerprint)
{
    if (fingerprint) {
        fingerprint->clear();
    }

    static const QRegularExpression tunnel(
        QStringLiteral("(?im)^[ \\t]*CURRENT[ \\t]+TUNNEL[ \\t]*:[ \\t]*(\\S+)[ \\t]*$"));
    const QRegularExpressionMatch match = tunnel.match(readme);
    if (!match.hasMatch()) {
        return Result<Endpoint>::error(QStringLiteral("readme_has_no_tunnel"));
    }

    const Result<Endpoint> endpoint = parseEndpoint(match.captured(1));
    if (endpoint.isError()) {
        return endpoint;
    }

    if (fingerprint) {
        static const QRegularExpression pin(
            QStringLiteral("(?im)^[ \\t]*CURRENT[ \\t]+FINGERPRINT[ \\t]*:[ \\t]*"
                           "([0-9A-Fa-f:]{64,95})[ \\t]*$"));
        const QRegularExpressionMatch pinMatch = pin.match(readme);
        if (pinMatch.hasMatch()) {
            *fingerprint = pinMatch.captured(1);
        }
    }

    return endpoint;
}

QString TunnelResolver::readTextFile(const QString &path, bool *exists) const
{
    const QFileInfo info(path);
    if (exists) {
        *exists = info.exists();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

Result<Endpoint> TunnelResolver::resolve()
{
    bool configExists = false;
    const QString configText = readTextFile(m_configPath, &configExists);

    bool configBroken = false;
    if (!configText.trimmed().isEmpty()) {
        const Result<Endpoint> fromConfig = parseEndpoint(configText);
        if (fromConfig.isOk()) {
            m_source = Source::Config;
            m_needsManualEntry = false;
            m_lastError.clear();
            return fromConfig;
        }
        configBroken = true;
        m_lastError = fromConfig.error();
    } else if (configExists) {
        configBroken = true;
        m_lastError = QStringLiteral("empty");
    }

    const QString cacheText = readTextFile(m_cachePath);
    if (!cacheText.trimmed().isEmpty()) {
        const Result<Endpoint> fromCache = parseEndpoint(cacheText);
        if (fromCache.isOk()) {
            m_source = Source::Cache;
            m_needsManualEntry = configBroken;
            return fromCache;
        }
        if (m_lastError.isEmpty()) {
            m_lastError = fromCache.error();
        }
    }

    m_source = Source::Default;
    m_needsManualEntry = configBroken;
    return Result<Endpoint>::ok(Endpoint(QStringLiteral("http"),
                                         QStringLiteral("127.0.0.1"), 8765));
}

void TunnelResolver::fetchFromReadme()
{
    if (m_fetching || !m_network || !m_readmeUrl.isValid()) {
        return;
    }
    m_fetching = true;

    QNetworkRequest request(m_readmeUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MINI-Messenger"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_fetching = false;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = reply->errorString();
            emit failed(m_lastError);
            return;
        }

        const QString readme = QString::fromUtf8(reply->readAll());
        QString fingerprint;
        const Result<Endpoint> parsed = parseReadme(readme, &fingerprint);
        if (parsed.isError()) {
            m_lastError = parsed.error();
            emit failed(m_lastError);
            return;
        }

        if (!saveEndpoint(parsed.value())) {
            m_lastError = QStringLiteral("cannot_save_endpoint");
            emit failed(m_lastError);
            return;
        }

        m_source = Source::Config;
        m_needsManualEntry = false;
        m_lastError.clear();
        if (!fingerprint.isEmpty()) {
            m_readmeFingerprint = fingerprint;
            emit fingerprintResolved(fingerprint);
        }
        emit resolved(parsed.value(), QStringLiteral("readme"));
    });
}

bool TunnelResolver::saveEndpoint(const Endpoint &endpoint)
{
    if (!endpoint.isValid()) {
        return false;
    }
    const QByteArray data = endpoint.toString().toUtf8() + '\n';
    const bool cacheSaved = writeAtomic(m_cachePath, data);
    const bool configSaved = writeAtomic(m_configPath, data);
    return cacheSaved && configSaved;
}

} // namespace mini::net
