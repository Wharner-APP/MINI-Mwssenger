#pragma once

#include "core/Result.h"
#include "net/Endpoint.h"

#include <QMetaType>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

namespace mini::net {

// Resolves the server address from config/connect.ip, the last known address,
// or the README published on GitHub. Network access is asynchronous: the GUI
// thread never waits for HTTP. Tests use a local HTTP stub.
class TunnelResolver : public QObject
{
    Q_OBJECT

public:
    enum class Source {
        Config, // config/connect.ip was valid
        Cache,  // fallback to the last known good address
        Default // built-in 127.0.0.1:8765
    };
    Q_ENUM(Source)

    TunnelResolver(QString configPath, QString cachePath, QUrl readmeUrl,
                   QNetworkAccessManager *network, QObject *parent = nullptr);

    // https://raw.githubusercontent.com/Wharner-APP/MINI-Mwssenger/main/README.md
    [[nodiscard]] static QUrl defaultReadmeUrl();

    // Accepted: "host:port", "http://host:port", "https://host:port".
    // BOM, surrounding whitespace and a trailing newline are ignored.
    [[nodiscard]] static Result<Endpoint> parseEndpoint(const QString &text);

    // Extracts "CURRENT TUNNEL: host:port" (and the optional
    // "CURRENT FINGERPRINT: <sha256>") from the README.
    [[nodiscard]] static Result<Endpoint> parseReadme(const QString &readme,
                                                      QString *fingerprint = nullptr);

    // Offline resolution: config -> cache -> default. Never touches the network.
    [[nodiscard]] Result<Endpoint> resolve();

    // Starts an asynchronous README download. Emits resolved()/failed().
    void fetchFromReadme();

    // Persists the address to both connect.ip and the cache.
    bool saveEndpoint(const Endpoint &endpoint);

    [[nodiscard]] Source source() const { return m_source; }
    [[nodiscard]] bool needsManualEntry() const { return m_needsManualEntry; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString readmeFingerprint() const { return m_readmeFingerprint; }
    [[nodiscard]] bool isFetching() const { return m_fetching; }

signals:
    void resolved(const mini::net::Endpoint &endpoint, const QString &origin);
    void fingerprintResolved(const QString &sha256);
    void failed(const QString &reason);

private:
    [[nodiscard]] QString readTextFile(const QString &path, bool *exists = nullptr) const;

    QString m_configPath;
    QString m_cachePath;
    QUrl m_readmeUrl;
    QNetworkAccessManager *m_network = nullptr;

    Source m_source = Source::Default;
    bool m_needsManualEntry = false;
    bool m_fetching = false;
    QString m_lastError;
    QString m_readmeFingerprint;
};

} // namespace mini::net

Q_DECLARE_METATYPE(mini::net::Endpoint)
