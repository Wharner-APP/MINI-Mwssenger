#pragma once

#include "core/Result.h"
#include "crypto/Kdf.h"
#include "crypto/SignInKey.h"
#include "net/Endpoint.h"
#include "services/SignInService.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace mini::net {
class TlsPin;
} // namespace mini::net

namespace mini::services {

struct CaptchaInfo
{
    QString provider = QStringLiteral("none");
    bool required = false;
    QString siteKey;
};

struct PreparedAuth
{
    QByteArray saltFile;
    QByteArray saltPw;
    QString nonce;
    CaptchaInfo captcha;
};

struct AuthSession
{
    QString accessToken;
    QString refreshToken;
    QString deviceId;
    QString userId;
    QString login;
    QString displayName;
};

struct AuthDevice
{
    QString deviceId;
    QString name;
    QString platform;
    qint64 lastSeenAt = 0;
    bool current = false;
};

struct LoginAvailability
{
    bool available = false;
    QString reason;
};

struct RegistrationResult
{
    crypto::SignInMaterial material;
    QString filePath;
    bool usedFallbackDirectory = false;
    AuthSession session;
};

struct ReissueResult
{
    crypto::SignInMaterial material;
    QString filePath;
};

// Account/auth orchestration. Network calls follow the wire contract of
// server/app/routes/auth.py; the server never sees K_enc/K_mac or sign.in.
class AuthService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    // Re-encrypts the local database from one K_enc branch to another.
    // Returning an error rolls the reissue back before the server is touched.
    using DatabaseRekey = std::function<Result<bool>(const QByteArray &oldKenc,
                                                     const QByteArray &newKenc)>;

    explicit AuthService(net::Endpoint endpoint, QObject *parent = nullptr);
    ~AuthService() override;

    void setEndpoint(const net::Endpoint &endpoint);
    [[nodiscard]] net::Endpoint endpoint() const { return m_endpoint; }

    // Mandatory for production: auth requests are refused unless the endpoint
    // is HTTPS and this pin has an expected SHA-256 fingerprint.
    void setTlsPin(net::TlsPin *pin) { m_pin = pin; }
    [[nodiscard]] net::TlsPin *tlsPin() const { return m_pin; }

    // Test-only escape hatch for the plain-HTTP stub server. Never set in the
    // application; the default is a hard TLS requirement.
    void setAllowInsecureTransportForTesting(bool allow) { m_allowInsecure = allow; }

    // Test/portable seam for the local database and keys.bin. Empty = dataDir().
    void setLocalDataPaths(const QString &databasePath, const QString &keysPath);

    // Persisted by the caller (config/settings.ini) so one account keeps one
    // device. Clearing it makes the next request mint a fresh installation
    // id; registration always does that because each account gets its own
    // device row.
    void setDeviceId(const QString &deviceId);
    void clearDeviceId();
    [[nodiscard]] QString deviceId() const;

    // Test/portable seam: where sign.in files are written. Empty = Downloads.
    void setSignInDirectory(const QString &directory);
    [[nodiscard]] QString signInPath() const { return m_signInPath; }
    void setSignInPath(const QString &path) { m_signInPath = path; }

    void setDatabaseRekey(DatabaseRekey rekey) { m_rekey = std::move(rekey); }

    [[nodiscard]] bool hasSession() const { return !m_session.accessToken.isEmpty(); }
    [[nodiscard]] const AuthSession &session() const { return m_session; }
    [[nodiscard]] const crypto::FileKeys &fileKeys() const { return m_fileKeys; }

    // Client-side field validation (same rules as the server).
    [[nodiscard]] static Result<QString> validateLogin(const QString &login);
    [[nodiscard]] static Result<QString> validateDisplayName(const QString &name);
    [[nodiscard]] static Result<QString> validatePassword(const QString &password,
                                                          const QString &login,
                                                          const QString &displayName);
    // 0..4, used as a hint only; never blocks registration.
    [[nodiscard]] static int passwordScore(const QString &password);

    void checkLogin(const QString &login, Done<LoginAvailability> done);

    // prepare -> generate sign.in -> Argon2id -> register -> save file.
    void registerAccount(const QString &login, const QString &displayName,
                         const QString &password, const QString &captchaTicket,
                         Done<RegistrationResult> done);

    // Reads and checks sign.in locally; invalid files never reach the network.
    void login(const QString &login, const QString &password,
               const QString &signInPath, const QString &captchaTicket,
               Done<AuthSession> done);

    void listDevices(Done<QList<AuthDevice>> done);
    void revokeDevice(const QString &deviceId, Done<bool> done);

    void changePassword(const QString &oldPassword, const QString &newPassword,
                        Done<bool> done);

    // New sign.in + local re-encryption. If the server call fails, the local
    // database is re-encrypted back and the old file keeps working.
    void reissueSignIn(Done<ReissueResult> done);

    // Typed login must equal the signed-in login; confirmation is the second
    // explicit "yes" from the UI.
    void deleteAccount(const QString &typedLogin, const QString &password,
                       bool confirmed, Done<bool> done);

    // Clears tokens and key material (logout / account deletion).
    void clearSession();

    // POST /auth/logout, then wipes keys.bin/session.bin/db.wrap locally.
    void logout(Done<bool> done);

signals:
    void authenticationRequired();

private:
    void request(const QByteArray &method, const QString &path,
                 const QJsonObject &body, bool authenticated,
                 std::function<void(Result<QJsonObject>)> done,
                 int timeoutMs = 15000);
    void handleSession(const QJsonObject &root);
    [[nodiscard]] QString storedDeviceId();
    [[nodiscard]] bool transportAllowed() const;
    [[nodiscard]] Result<bool> rekeyDatabase(const QByteArray &fromKenc,
                                             const QByteArray &toKenc);
    [[nodiscard]] Result<bool> rekeyLocal(const QByteArray &oldKenc,
                                          const QByteArray &newKenc);
    void removeLocalSecrets(bool removeDatabase, bool removeMedia);

    net::Endpoint m_endpoint;
    net::TlsPin *m_pin = nullptr;
    bool m_allowInsecure = false;
    QString m_databasePath;
    QString m_keysPath;
    QNetworkAccessManager *m_network = nullptr;
    QString m_accessToken;
    QString m_refreshToken;
    QString m_deviceId;
    QString m_login;
    QByteArray m_saltPw;
    crypto::FileKeys m_fileKeys;
    AuthSession m_session;
    DatabaseRekey m_rekey;
    SignInService m_signIns;
    QString m_signInPath;
};

} // namespace mini::services
