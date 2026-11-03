#pragma once

#include "core/Result.h"
#include "net/Endpoint.h"

#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace mini::net {
class TlsPin;
} // namespace mini::net

namespace mini::services {

struct CaptchaChallenge
{
    QString challengeId;
    QString svg;
    int expiresIn = 120;
};

// hCaptcha is preferred when a site key is configured; the built-in 6-symbol
// challenge is the fallback (and the only provider before setup_captcha.bat).
class CaptchaService : public QObject
{
    Q_OBJECT

public:
    enum class Provider { None, Builtin, HCaptcha };
    Q_ENUM(Provider)

    static constexpr int kTimeoutMs = 60000;

    explicit CaptchaService(net::Endpoint endpoint, QString siteKey = QString(),
                            QObject *parent = nullptr);

    void setEndpoint(const net::Endpoint &endpoint) { m_endpoint = endpoint; }
    void setSiteKey(const QString &siteKey);

    // Same transport policy as AuthService: HTTPS + a configured pin.
    void setTlsPin(net::TlsPin *pin) { m_pin = pin; }
    void setAllowInsecureTransportForTesting(bool allow) { m_allowInsecure = allow; }

    [[nodiscard]] Provider provider() const;
    [[nodiscard]] bool required() const { return provider() != Provider::None; }
    [[nodiscard]] QString siteKey() const { return m_siteKey; }

    // Built-in challenge: POST /api/v1/captcha/challenge.
    void requestBuiltin(std::function<void(Result<CaptchaChallenge>)> done);
    // POST /api/v1/captcha/verify; the ticket is passed to register/login.
    void verifyBuiltin(const QString &challengeId, const QString &answer,
                       std::function<void(Result<QString>)> done);

    // Local page for QWebEngineView; posts the token through QWebChannel.
    [[nodiscard]] static QString hcaptchaPageHtml(const QString &siteKey);

signals:
    // Emitted when the external widget cannot be used, so the dialog can
    // switch to the built-in challenge without losing the form.
    void externalUnavailable();

private:
    [[nodiscard]] bool transportAllowed() const;

    net::Endpoint m_endpoint;
    net::TlsPin *m_pin = nullptr;
    bool m_allowInsecure = false;
    QString m_siteKey;
    QNetworkAccessManager *m_network = nullptr;
};

} // namespace mini::services
