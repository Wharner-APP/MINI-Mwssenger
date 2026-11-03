#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"
#include "net/Endpoint.h"

#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mini::net {

class TlsPin;

// Typed, asynchronous HTTP client. Every method reports via a callback and
// never throws; no JSON key or raw body leaves this class.
class ApiClient : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Callback = std::function<void(ApiResult<T>)>;

    explicit ApiClient(const Endpoint &endpoint, TlsPin *pin = nullptr,
                       QObject *parent = nullptr);
    ApiClient(const Endpoint &endpoint, TlsPin *pin,
              QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ApiClient() override;

    void setEndpoint(const Endpoint &endpoint) { m_endpoint = endpoint; }
    [[nodiscard]] Endpoint endpoint() const { return m_endpoint; }

    void setTlsPin(TlsPin *pin) { m_pin = pin; }
    [[nodiscard]] TlsPin *tlsPin() const { return m_pin; }

    void setAccessToken(const QString &token) { m_accessToken = token; }
    [[nodiscard]] QString accessToken() const { return m_accessToken; }

    void setRefreshToken(const QString &token) { m_refreshToken = token; }
    [[nodiscard]] QString refreshToken() const { return m_refreshToken; }

    void setDeviceId(const QString &deviceId) { m_deviceId = deviceId; }
    [[nodiscard]] QString deviceId() const { return m_deviceId; }

    void setProtocolVersion(int version);
    [[nodiscard]] int protocolVersion() const { return m_protocolVersion; }

    // Primarily for tests: lowers the 15 s HTTP timeout.
    void setTransferTimeoutMs(int milliseconds) { m_transferTimeoutMs = milliseconds; }

    void prepare(const QString &login, Callback<AuthPrepare> callback);
    void checkLogin(const QString &login, Callback<LoginAvailability> callback);
    void registerAccount(const RegisterRequest &request, Callback<AuthSession> callback);
    void login(const LoginRequest &request, Callback<AuthSession> callback);
    void sendEnvelopes(const QList<Envelope> &envelopes, Callback<SendResult> callback);
    void pullMail(const QString &deviceId, const QString &cursor, Callback<MailBatch> callback);
    void ackMail(const QStringList &mailIds, Callback<AckResult> callback);
    void getMe(Callback<AccountProfile> callback);
    void patchMe(const ProfilePatch &patch, Callback<AccountProfile> callback);
    void changePassword(const ChangePasswordRequest &request, Callback<Empty> callback);
    void reissueSignIn(const ReissueRequest &request, Callback<ReissueResult> callback);
    void deleteAccount(const DeleteAccountRequest &request, Callback<Empty> callback);
    void requestWsTicket(Callback<WsTicket> callback);

    // Public prekey directory used by the per-chat session key.
    void putPrekeys(const PeerKeys &keys, Callback<Empty> callback);
    void fetchPeerKeys(const QString &login, Callback<PeerKeys> callback);
    // Exact-login public profile (GET /api/v1/users/{login}).
    void lookupUser(const QString &login, Callback<AccountProfile> callback);

signals:
    // A 401 from any endpoint. The caller must stop retrying and sign in again.
    void authenticationRequired();
    void certificateMismatch(const QString &actual, const QString &expected);
    // Warning only (HTTPS): the pin is missing or malformed, so the request
    // relies on CA validation. WSS treats the same condition as a failure.
    void certificateWarning(const QString &reason, const QString &expected);
    void protocolVersionMismatch(int serverVersion);

private:
    using RawCallback = std::function<void(ApiResult<QByteArray>)>;

    void send(const QByteArray &method, const QString &path, const QJsonObject &body,
              bool authenticated, RawCallback callback);
    [[nodiscard]] ApiError tlsErrorFor(QNetworkReply *reply) const;

    Endpoint m_endpoint;
    TlsPin *m_pin = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    bool m_ownsNetwork = false;
    QString m_accessToken;
    QString m_refreshToken;
    QString m_deviceId;
    int m_protocolVersion = 1;
    int m_transferTimeoutMs = 15000;
};

} // namespace mini::net
