#include "services/CaptchaService.h"

#include "net/TlsPin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslError>
#include <QUrl>

namespace mini::services {

namespace {

QString jsString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return escaped;
}

Result<QJsonObject> parseReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    QJsonObject root = document.object();

    if (status == 0 && reply->error() != QNetworkReply::NoError) {
        return Result<QJsonObject>::error(QStringLiteral("network"));
    }
    if (status >= 400) {
        const QJsonObject error = root.value(QStringLiteral("error")).toObject();
        const QString code = error.value(QStringLiteral("code")).toString();
        return Result<QJsonObject>::error(code.isEmpty() ? QStringLiteral("http_%1").arg(status)
                                                         : code);
    }
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<QJsonObject>::error(QStringLiteral("protocol"));
    }
    return Result<QJsonObject>::ok(root);
}

} // namespace

CaptchaService::CaptchaService(net::Endpoint endpoint, QString siteKey, QObject *parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
    , m_siteKey(std::move(siteKey))
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_network, &QNetworkAccessManager::sslErrors, this,
            [this](QNetworkReply *reply, const QList<QSslError> &) {
                if (!reply) {
                    return;
                }
                const QSslCertificate certificate =
                    reply->sslConfiguration().peerCertificate();
                if (m_pin && m_pin->shouldIgnore(certificate)) {
                    reply->ignoreSslErrors();
                }
            });
}

bool CaptchaService::transportAllowed() const
{
    if (m_allowInsecure) {
        return true;
    }
    if (!m_endpoint.isSecure()) {
        return false;
    }
    return m_pin && m_pin->isConfigured() && !m_pin->isMalformed();
}

void CaptchaService::setSiteKey(const QString &siteKey)
{
    m_siteKey = siteKey;
}

CaptchaService::Provider CaptchaService::provider() const
{
    return m_siteKey.isEmpty() ? Provider::Builtin : Provider::HCaptcha;
}

void CaptchaService::requestBuiltin(std::function<void(Result<CaptchaChallenge>)> done)
{
    if (!done) {
        return;
    }
    if (!transportAllowed()) {
        done(Result<CaptchaChallenge>::error(
            m_endpoint.isSecure() ? QStringLiteral("tls_pin_required")
                                  : QStringLiteral("tls_required")));
        return;
    }
    if (!m_endpoint.isValid()) {
        done(Result<CaptchaChallenge>::error(QStringLiteral("invalid_endpoint")));
        return;
    }

    QNetworkRequest request(m_endpoint.httpUrl(QStringLiteral("/api/v1/captcha/challenge")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->post(request, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && m_endpoint.isSecure() && !m_allowInsecure) {
            const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
            if (!m_pin || m_pin->check(certificate) != net::TlsPin::Outcome::Match) {
                done(Result<CaptchaChallenge>::error(QStringLiteral("tls_fingerprint")));
                return;
            }
        }
        const Result<QJsonObject> parsed = parseReply(reply);
        if (parsed.isError()) {
            done(Result<CaptchaChallenge>::error(parsed.error()));
            return;
        }
        const QJsonObject root = parsed.value();
        CaptchaChallenge challenge;
        challenge.challengeId = root.value(QStringLiteral("challenge_id")).toString();
        challenge.svg = root.value(QStringLiteral("image_svg")).toString();
        challenge.expiresIn = root.value(QStringLiteral("expires_in")).toInt(120);
        if (challenge.challengeId.isEmpty() || challenge.svg.isEmpty()) {
            done(Result<CaptchaChallenge>::error(QStringLiteral("protocol")));
            return;
        }
        done(Result<CaptchaChallenge>::ok(challenge));
    });
}

void CaptchaService::verifyBuiltin(const QString &challengeId, const QString &answer,
                                   std::function<void(Result<QString>)> done)
{
    if (!done) {
        return;
    }
    if (!transportAllowed()) {
        done(Result<QString>::error(
            m_endpoint.isSecure() ? QStringLiteral("tls_pin_required")
                                  : QStringLiteral("tls_required")));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("challenge_id"), challengeId);
    body.insert(QStringLiteral("answer"), answer);

    QNetworkRequest request(m_endpoint.httpUrl(QStringLiteral("/api/v1/captcha/verify")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && m_endpoint.isSecure() && !m_allowInsecure) {
            const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
            if (!m_pin || m_pin->check(certificate) != net::TlsPin::Outcome::Match) {
                done(Result<QString>::error(QStringLiteral("tls_fingerprint")));
                return;
            }
        }
        const Result<QJsonObject> parsed = parseReply(reply);
        if (parsed.isError()) {
            done(Result<QString>::error(parsed.error()));
            return;
        }
        const QString ticket = parsed.value().value(QStringLiteral("ticket")).toString();
        if (ticket.isEmpty()) {
            done(Result<QString>::error(QStringLiteral("protocol")));
            return;
        }
        done(Result<QString>::ok(ticket));
    });
}

QString CaptchaService::hcaptchaPageHtml(const QString &siteKey)
{
    return QStringLiteral(R"(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  html, body { margin: 0; padding: 0; background: #1B1D22; color: #E8E9EC;
               font-family: 'Segoe UI', sans-serif; }
  #wrap { display: flex; align-items: center; justify-content: center;
          min-height: 100vh; }
  #status { position: fixed; inset: 0; display: flex; align-items: center;
            justify-content: center; color: #A0A6AE; font-size: 14px; }
</style>
</head>
<body>
<div id="status">%2</div>
<div id="wrap"><div id="captcha"></div></div>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<script src="https://js.hcaptcha.com/1/api.js?render=explicit&onload=hcaptchaReady" async defer></script>
<script>
  var bridge = null;
  var rendered = false;
  function fail(reason) {
    if (bridge) { bridge.onError(String(reason)); }
  }
  new QWebChannel(qt.webChannelTransport, function (channel) {
    bridge = channel.objects.bridge;
    if (rendered) { fail('ready'); }
  });
  function hcaptchaReady() {
    try {
      hcaptcha.render('captcha', {
        sitekey: '%1',
        theme: 'dark',
        callback: function (token) {
          document.getElementById('status').style.display = 'none';
          if (bridge) { bridge.onToken(token); }
        },
        'expired-callback': function () { fail('expired'); },
        'error-callback': function (error) { fail(error || 'error'); }
      });
      rendered = true;
    } catch (error) {
      fail(error);
    }
  }
  window.addEventListener('error', function (event) { fail(event.message); });
  setTimeout(function () { if (!rendered) { fail('timeout'); } }, 20000);
</script>
</body>
</html>)")
        .arg(jsString(siteKey), QStringLiteral("Loading hCaptcha…"));
}

} // namespace mini::services
