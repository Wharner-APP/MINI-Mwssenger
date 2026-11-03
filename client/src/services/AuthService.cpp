#include "services/AuthService.h"

#include "app/Paths.h"
#include "crypto/IdentityKeys.h"
#include "crypto/Kdf.h"
#include "net/TlsPin.h"
#include "storage/BodyCipher.h"
#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslError>
#include <QSysInfo>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
#include <QVector>

#include <sodium.h>

namespace mini::services {

namespace {

const QStringList &reservedLogins()
{
    static const QStringList reserved = {
        QStringLiteral("admin"), QStringLiteral("root"), QStringLiteral("support"),
        QStringLiteral("telegram"), QStringLiteral("mini"), QStringLiteral("system"),
        QStringLiteral("help"), QStringLiteral("security"), QStringLiteral("official"),
        QStringLiteral("moderator"), QStringLiteral("bot"), QStringLiteral("channel"),
        QStringLiteral("group"), QStringLiteral("null"), QStringLiteral("undefined")};
    return reserved;
}

const QStringList &commonPasswords()
{
    // Placeholder for the full top-10000 list required by §28.4.
    static const QStringList common = {
        QStringLiteral("password"), QStringLiteral("password1"), QStringLiteral("qwerty123"),
        QStringLiteral("12345678"), QStringLiteral("123456789"), QStringLiteral("11111111"),
        QStringLiteral("letmein1"), QStringLiteral("admin123"), QStringLiteral("welcome1"),
        QStringLiteral("iloveyou"), QStringLiteral("abc12345"), QStringLiteral("00000000")};
    return common;
}

bool isForbiddenNameChar(QChar c)
{
    const ushort code = c.unicode();
    if (code <= 0x001F || (code >= 0x007F && code <= 0x009F)) {
        return true;
    }
    if (code >= 0x200E && code <= 0x200F) {
        return true;
    }
    if (code >= 0x202A && code <= 0x202E) {
        return true;
    }
    if (code >= 0x2066 && code <= 0x2069) {
        return true;
    }
    return c.category() == QChar::Other_Format;
}

QByteArray decodeBase64Any(const QString &value)
{
    QString normalized = value;
    normalized.replace(QLatin1Char('-'), QLatin1Char('+'));
    normalized.replace(QLatin1Char('_'), QLatin1Char('/'));
    while (normalized.size() % 4 != 0) {
        normalized.append(QLatin1Char('='));
    }
    return QByteArray::fromBase64(normalized.toLatin1());
}

QString encodeBase64(const QByteArray &value)
{
    return QString::fromLatin1(value.toBase64());
}

Result<QJsonObject> parseJsonReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 0 && reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::TimeoutError
            || reply->error() == QNetworkReply::OperationCanceledError) {
            return Result<QJsonObject>::error(QStringLiteral("timeout"));
        }
        return Result<QJsonObject>::error(QStringLiteral("network"));
    }
    if (status >= 400) {
        const QJsonObject error = QJsonDocument::fromJson(data).object()
                                      .value(QStringLiteral("error"))
                                      .toObject();
        const QString code = error.value(QStringLiteral("code")).toString();
        if (!code.isEmpty()) {
            return Result<QJsonObject>::error(code);
        }
        return Result<QJsonObject>::error(QStringLiteral("http_%1").arg(status));
    }
    if (data.trimmed().isEmpty()) {
        return Result<QJsonObject>::ok(QJsonObject());
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<QJsonObject>::error(QStringLiteral("protocol"));
    }
    return Result<QJsonObject>::ok(document.object());
}

QJsonObject deviceBody(const QString &deviceId)
{
    QJsonObject device;
    device.insert(QStringLiteral("device_id"), deviceId);
    device.insert(QStringLiteral("name"), QSysInfo::machineHostName());
    device.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    return device;
}

struct DerivedFileKeys
{
    DerivedFileKeys(Result<crypto::FileKeys> fileValue, Result<QByteArray> passwordValue)
        : file(std::move(fileValue)), password(std::move(passwordValue)) {}
    Result<crypto::FileKeys> file;
    Result<QByteArray> password;
};

struct DerivedPasswordKeys
{
    DerivedPasswordKeys(Result<QByteArray> oldValue, Result<QByteArray> newValue)
        : oldKey(std::move(oldValue)), newKey(std::move(newValue)) {}
    Result<QByteArray> oldKey;
    Result<QByteArray> newKey;
};

} // namespace

AuthService::AuthService(net::Endpoint endpoint, QObject *parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
    , m_network(new QNetworkAccessManager(this))
{
    m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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

AuthService::~AuthService()
{
    clearSession();
}

void AuthService::setEndpoint(const net::Endpoint &endpoint)
{
    m_endpoint = endpoint;
}

void AuthService::setLocalDataPaths(const QString &databasePath, const QString &keysPath)
{
    m_databasePath = databasePath;
    m_keysPath = keysPath;
}

bool AuthService::transportAllowed() const
{
    if (m_allowInsecure) {
        return true;
    }
    if (!m_endpoint.isSecure()) {
        return false;
    }
    return m_pin && m_pin->isConfigured() && !m_pin->isMalformed();
}

Result<bool> AuthService::rekeyDatabase(const QByteArray &fromKenc,
                                         const QByteArray &toKenc)
{
    if (fromKenc.size() != crypto::Kdf::kKeyBytes
        || toKenc.size() != crypto::Kdf::kKeyBytes) {
        return Result<bool>::error(QStringLiteral("rekey.keys"));
    }
    const Result<QByteArray> fromDb = crypto::Kdf::deriveDbKey(fromKenc);
    const Result<QByteArray> toDb = crypto::Kdf::deriveDbKey(toKenc);
    if (fromDb.isError() || toDb.isError()) {
        return Result<bool>::error(QStringLiteral("rekey.keys"));
    }
    const QString databasePath = m_databasePath.isEmpty()
        ? QDir(Paths::dataDir()).filePath(QStringLiteral("messages.db"))
        : m_databasePath;
    if (!QFileInfo::exists(databasePath)) {
        return Result<bool>::ok(true);
    }

    Result<std::unique_ptr<storage::Database>> opened =
        storage::Database::open(databasePath, fromDb.value());
    if (opened.isError()) {
        return Result<bool>::error(QStringLiteral("rekey.database"));
    }
    std::unique_ptr<storage::Database> database = std::move(opened.value());
    const Result<bool> rekeyed = database->transaction(
        [&fromDb, &toDb](storage::Database &db) -> Result<bool> {
            storage::BodyCipher fromCipher(fromDb.value());
            storage::BodyCipher toCipher(toDb.value());
            if (!fromCipher.isValid() || !toCipher.isValid()) {
                return Result<bool>::error(QStringLiteral("rekey.database"));
            }

            struct MessageRow
            {
                QString clientMsgId;
                QString chatId;
                QByteArray body;
            };
            QVector<MessageRow> messages;
            {
                storage::detail::Statement select(
                    db.handle(), "SELECT client_msg_id, chat_id, body FROM messages");
                if (!select.isOk()) {
                    return Result<bool>::error(
                        storage::detail::sqlError(db.handle(), "rekey.messages"));
                }
                while (select.step() == SQLITE_ROW) {
                    messages.append({select.text(0), select.text(1), select.blob(2)});
                }
            }
            storage::detail::Statement updateMessage(
                db.handle(), "UPDATE messages SET body = ? WHERE client_msg_id = ?");
            if (!updateMessage.isOk()) {
                return Result<bool>::error(
                    storage::detail::sqlError(db.handle(), "rekey.messages.update"));
            }
            for (const MessageRow &row : messages) {
                const QByteArray aad = storage::messageAad(row.chatId, row.clientMsgId);
                const Result<QString> plain = fromCipher.decrypt(row.body, aad);
                if (plain.isError()) {
                    return Result<bool>::error(QStringLiteral("rekey.body"));
                }
                const Result<QByteArray> sealed = toCipher.encrypt(plain.value(), aad);
                if (sealed.isError()) {
                    return Result<bool>::error(QStringLiteral("rekey.body"));
                }
                sqlite3_reset(updateMessage.get());
                sqlite3_clear_bindings(updateMessage.get());
                updateMessage.bindBlob(1, sealed.value());
                updateMessage.bindText(2, row.clientMsgId);
                if (updateMessage.step() != SQLITE_DONE) {
                    return Result<bool>::error(
                        storage::detail::sqlError(db.handle(), "rekey.messages.write"));
                }
            }

            QVector<QPair<QString, QByteArray>> chats;
            {
                storage::detail::Statement select(
                    db.handle(), "SELECT chat_id, last_message_preview FROM chats");
                if (!select.isOk()) {
                    return Result<bool>::error(
                        storage::detail::sqlError(db.handle(), "rekey.chats"));
                }
                while (select.step() == SQLITE_ROW) {
                    chats.append({select.text(0), select.blob(1)});
                }
            }
            storage::detail::Statement updateChat(
                db.handle(),
                "UPDATE chats SET last_message_preview = ? WHERE chat_id = ?");
            if (!updateChat.isOk()) {
                return Result<bool>::error(
                    storage::detail::sqlError(db.handle(), "rekey.chats.update"));
            }
            for (const auto &row : chats) {
                const QByteArray aad = storage::chatPreviewAad(row.first);
                const Result<QString> plain = fromCipher.decrypt(row.second, aad);
                if (plain.isError()) {
                    return Result<bool>::error(QStringLiteral("rekey.preview"));
                }
                const Result<QByteArray> sealed = toCipher.encrypt(plain.value(), aad);
                if (sealed.isError()) {
                    return Result<bool>::error(QStringLiteral("rekey.preview"));
                }
                sqlite3_reset(updateChat.get());
                sqlite3_clear_bindings(updateChat.get());
                updateChat.bindBlob(1, sealed.value());
                updateChat.bindText(2, row.first);
                if (updateChat.step() != SQLITE_DONE) {
                    return Result<bool>::error(
                        storage::detail::sqlError(db.handle(), "rekey.chats.write"));
                }
            }
            return Result<bool>::ok(true);
        });
    database->close();
    if (rekeyed.isError()) {
        return Result<bool>::error(QStringLiteral("rekey.database"));
    }
    return Result<bool>::ok(true);
}

Result<bool> AuthService::rekeyLocal(const QByteArray &oldKenc, const QByteArray &newKenc)
{
    if (oldKenc.size() != crypto::Kdf::kKeyBytes
        || newKenc.size() != crypto::Kdf::kKeyBytes) {
        return Result<bool>::error(QStringLiteral("rekey.keys"));
    }
    const Result<QByteArray> oldWrap = crypto::Kdf::deriveKeyWrap(oldKenc);
    const Result<QByteArray> newWrap = crypto::Kdf::deriveKeyWrap(newKenc);
    if (oldWrap.isError() || newWrap.isError()) {
        return Result<bool>::error(QStringLiteral("rekey.keys"));
    }

    // 1. Database: one SQLite transaction, so a failure leaves it untouched.
    const Result<bool> databaseRekeyed = rekeyDatabase(oldKenc, newKenc);
    if (databaseRekeyed.isError()) {
        return databaseRekeyed;
    }

    auto rollbackDatabase = [this, oldKenc, newKenc] {
        return rekeyDatabase(newKenc, oldKenc);
    };

    // 2. keys.bin. IdentityKeys::save writes through QSaveFile, which replaces
    //    the target atomically; the old file is never removed beforehand.
    const QString keysPath = m_keysPath.isEmpty()
        ? QDir(Paths::dataDir()).filePath(QStringLiteral("keys.bin"))
        : m_keysPath;
    if (QFileInfo::exists(keysPath)) {
        const QString deviceId = m_session.deviceId.isEmpty() ? m_deviceId
                                                              : m_session.deviceId;
        if (deviceId.isEmpty()) {
            return Result<bool>::error(rollbackDatabase().isError()
                                           ? QStringLiteral("rekey.rollback")
                                           : QStringLiteral("rekey.device"));
        }
        const Result<crypto::IdentityState> state =
            crypto::IdentityKeys::load(keysPath, oldWrap.value(), deviceId);
        if (state.isError()) {
            return Result<bool>::error(rollbackDatabase().isError()
                                           ? QStringLiteral("rekey.rollback")
                                           : QStringLiteral("rekey.keys_load"));
        }
        const Result<int> saved = crypto::IdentityKeys::save(
            state.value(), keysPath, newWrap.value(), deviceId);
        if (saved.isError()) {
            return Result<bool>::error(rollbackDatabase().isError()
                                           ? QStringLiteral("rekey.rollback")
                                           : QStringLiteral("rekey.keys_save"));
        }
        const Result<crypto::IdentityState> verified =
            crypto::IdentityKeys::load(keysPath, newWrap.value(), deviceId);
        if (verified.isError()) {
            // The new file cannot be read back; restore the old wrap.
            const Result<int> restored = crypto::IdentityKeys::save(
                state.value(), keysPath, oldWrap.value(), deviceId);
            const bool rolledBack = restored.isOk() && rollbackDatabase().isOk();
            return Result<bool>::error(rolledBack ? QStringLiteral("rekey.keys_save")
                                                  : QStringLiteral("rekey.rollback"));
        }
    }
    return Result<bool>::ok(true);
}
void AuthService::setDeviceId(const QString &deviceId)
{
    if (!deviceId.isEmpty()) {
        m_deviceId = deviceId;
    }
}

void AuthService::clearDeviceId()
{
    m_deviceId.clear();
}

QString AuthService::deviceId() const
{
    return m_deviceId;
}

void AuthService::setSignInDirectory(const QString &directory)
{
    m_signIns = SignInService(directory);
}

QString AuthService::storedDeviceId()
{
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    return m_deviceId;
}

Result<QString> AuthService::validateLogin(const QString &login)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-zA-Z][a-zA-Z0-9_]{3,23}$"));
    const QString value = login.trimmed();
    if (!pattern.match(value).hasMatch()) {
        return Result<QString>::error(QStringLiteral("login.invalid_format"));
    }
    const QString lower = value.toLower();
    if (reservedLogins().contains(lower) || lower.startsWith(QStringLiteral("bot_"))) {
        return Result<QString>::error(QStringLiteral("login.reserved"));
    }
    return Result<QString>::ok(value);
}

Result<QString> AuthService::validateDisplayName(const QString &name)
{
    const QString value = name.normalized(QString::NormalizationForm_C).trimmed();
    if (value.isEmpty() || value.size() > 64) {
        return Result<QString>::error(QStringLiteral("name.invalid"));
    }
    for (const QChar c : value) {
        if (isForbiddenNameChar(c)) {
            return Result<QString>::error(QStringLiteral("name.invalid"));
        }
    }
    return Result<QString>::ok(value);
}

int AuthService::passwordScore(const QString &password)
{
    int score = 0;
    if (password.size() >= 8) {
        ++score;
    }
    if (password.size() >= 12) {
        ++score;
    }
    bool upper = false;
    bool lower = false;
    bool digit = false;
    bool symbol = false;
    for (const QChar c : password) {
        upper = upper || c.isUpper();
        lower = lower || c.isLower();
        digit = digit || c.isDigit();
        symbol = symbol || (!c.isLetterOrNumber() && !c.isSpace());
    }
    if (upper && lower) {
        ++score;
    }
    if (digit && symbol) {
        ++score;
    }
    return qBound(0, score, 4);
}

Result<QString> AuthService::validatePassword(const QString &password,
                                              const QString &login,
                                              const QString &displayName)
{
    if (password.size() < 8 || password.size() > 128) {
        return Result<QString>::error(QStringLiteral("password.length"));
    }
    bool hasLetter = false;
    bool hasDigit = false;
    for (const QChar c : password) {
        hasLetter = hasLetter || c.isLetter();
        hasDigit = hasDigit || c.isDigit();
    }
    if (!hasLetter || !hasDigit) {
        return Result<QString>::error(QStringLiteral("password.composition"));
    }
    const QString lower = password.toLower();
    if (!login.isEmpty() && lower == login.trimmed().toLower()) {
        return Result<QString>::error(QStringLiteral("password.equals_login"));
    }
    if (!displayName.isEmpty() && lower == displayName.trimmed().toLower()) {
        return Result<QString>::error(QStringLiteral("password.equals_name"));
    }
    if (commonPasswords().contains(lower)) {
        return Result<QString>::error(QStringLiteral("password.common"));
    }
    return Result<QString>::ok(password);
}

void AuthService::request(const QByteArray &method, const QString &path,
                          const QJsonObject &body, bool authenticated,
                          std::function<void(Result<QJsonObject>)> done, int timeoutMs)
{
    if (!done) {
        return;
    }
    if (!transportAllowed()) {
        done(Result<QJsonObject>::error(
            m_endpoint.isSecure() ? QStringLiteral("tls_pin_required")
                                  : QStringLiteral("tls_required")));
        return;
    }
    if (!m_endpoint.isValid()) {
        done(Result<QJsonObject>::error(QStringLiteral("invalid_endpoint")));
        return;
    }
    QNetworkRequest request(m_endpoint.httpUrl(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "MINI-Messenger");
    request.setTransferTimeout(timeoutMs);
    if (authenticated && !m_accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    }

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_network->get(request);
    } else if (method == "POST") {
        reply = m_network->post(request, payload);
    } else {
        reply = m_network->sendCustomRequest(request, method, payload);
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, done, authenticated] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && m_endpoint.isSecure() && !m_allowInsecure) {
            const QSslCertificate certificate =
                reply->sslConfiguration().peerCertificate();
            if (!m_pin
                || m_pin->check(certificate) != net::TlsPin::Outcome::Match) {
                done(Result<QJsonObject>::error(QStringLiteral("tls_fingerprint")));
                return;
            }
        }
        const Result<QJsonObject> parsed = parseJsonReply(reply);
        if (status == 401 && authenticated && !m_accessToken.isEmpty()) {
            emit authenticationRequired();
        }
        done(parsed);
    });
}

void AuthService::handleSession(const QJsonObject &root)
{
    const QJsonObject account = root.value(QStringLiteral("account")).toObject();
    const QJsonObject device = root.value(QStringLiteral("device")).toObject();
    const QJsonObject tokens = root.value(QStringLiteral("tokens")).toObject();

    m_session = AuthSession();
    m_session.accessToken = tokens.value(QStringLiteral("access_token")).toString();
    m_session.refreshToken = tokens.value(QStringLiteral("refresh_token")).toString();
    m_session.deviceId = device.value(QStringLiteral("id")).toString();
    m_session.userId = account.value(QStringLiteral("id")).toString();
    m_session.login = account.value(QStringLiteral("login")).toString();
    m_session.displayName = account.value(QStringLiteral("display_name")).toString();

    m_accessToken = m_session.accessToken;
    m_refreshToken = m_session.refreshToken;
    if (!m_session.deviceId.isEmpty()) {
        m_deviceId = m_session.deviceId;
    }
    if (!m_session.login.isEmpty()) {
        m_login = m_session.login;
    }
}

void AuthService::checkLogin(const QString &login, Done<LoginAvailability> done)
{
    if (!done) {
        return;
    }
    const QString value = login.trimmed();
    const QString path = QStringLiteral("/api/v1/auth/check_login?login=")
        + QString::fromLatin1(QUrl::toPercentEncoding(value));
    request("GET", path, {}, false, [done](Result<QJsonObject> result) {
        if (result.isError()) {
            done(Result<LoginAvailability>::error(result.error()));
            return;
        }
        LoginAvailability availability;
        availability.available = result.value().value(QStringLiteral("available")).toBool();
        availability.reason = result.value().value(QStringLiteral("reason")).toString();
        done(Result<LoginAvailability>::ok(availability));
    });
}

void AuthService::registerAccount(const QString &login, const QString &displayName,
                                  const QString &password, const QString &captchaTicket,
                                  Done<RegistrationResult> done)
{
    if (!done) {
        return;
    }
    const Result<QString> loginValue = validateLogin(login);
    if (loginValue.isError()) {
        done(Result<RegistrationResult>::error(loginValue.error()));
        return;
    }
    const Result<QString> nameValue = validateDisplayName(displayName);
    if (nameValue.isError()) {
        done(Result<RegistrationResult>::error(nameValue.error()));
        return;
    }
    const Result<QString> passwordValue = validatePassword(password, loginValue.value(), nameValue.value());
    if (passwordValue.isError()) {
        done(Result<RegistrationResult>::error(passwordValue.error()));
        return;
    }

    checkLogin(loginValue.value(), [this, loginValue, nameValue, password, captchaTicket, done](
                                       Result<LoginAvailability> availability) {
        if (availability.isError()) {
            done(Result<RegistrationResult>::error(availability.error()));
            return;
        }
        if (!availability.value().available) {
            const QString reason = availability.value().reason.isEmpty()
                ? QStringLiteral("taken")
                : availability.value().reason;
            done(Result<RegistrationResult>::error(QStringLiteral("login.%1").arg(reason)));
            return;
        }

        request("POST", QStringLiteral("/api/v1/auth/prepare"),
                QJsonObject{{QStringLiteral("login"), loginValue.value()},
                            {QStringLiteral("purpose"), QStringLiteral("register")}},
                false, [this, loginValue, nameValue, password, captchaTicket, done](
                           Result<QJsonObject> prepared) {
            if (prepared.isError()) {
                done(Result<RegistrationResult>::error(prepared.error()));
                return;
            }
            const QJsonObject root = prepared.value();
            PreparedAuth prep;
            prep.saltFile = decodeBase64Any(root.value(QStringLiteral("salt_file")).toString());
            prep.saltPw = decodeBase64Any(root.value(QStringLiteral("salt_pw")).toString());
            prep.nonce = root.value(QStringLiteral("nonce")).toString();
            const QJsonObject captcha = root.value(QStringLiteral("captcha")).toObject();
            prep.captcha.provider = captcha.value(QStringLiteral("provider")).toString();
            prep.captcha.required = captcha.value(QStringLiteral("required")).toBool();
            prep.captcha.siteKey = captcha.value(QStringLiteral("site_key")).toString();
            if (prep.saltFile.size() != crypto::Kdf::kSaltBytes
                || prep.saltPw.size() != crypto::Kdf::kSaltBytes || prep.nonce.isEmpty()) {
                done(Result<RegistrationResult>::error(QStringLiteral("protocol")));
                return;
            }
            if (prep.captcha.required && captchaTicket.isEmpty()) {
                done(Result<RegistrationResult>::error(QStringLiteral("captcha.required")));
                return;
            }

            const Result<crypto::SignInMaterial> created = m_signIns.create();
            if (created.isError()) {
                done(Result<RegistrationResult>::error(created.error()));
                return;
            }
            const crypto::SignInMaterial material = created.value();

            auto derive = [material, prep, password] {
                return DerivedFileKeys(
                    crypto::Kdf::deriveFileKeys(material.parsed.normalized, prep.saltFile),
                    crypto::Kdf::derivePasswordKey(password, prep.saltPw));
            };
            QtConcurrent::run(derive).then(
                this, [this, loginValue, nameValue, captchaTicket, material, prep, done](
                          DerivedFileKeys keys) {
                    if (keys.file.isError()) {
                        done(Result<RegistrationResult>::error(keys.file.error()));
                        return;
                    }
                    if (keys.password.isError()) {
                        done(Result<RegistrationResult>::error(keys.password.error()));
                        return;
                    }
                    QJsonObject body;
                    body.insert(QStringLiteral("login"), loginValue.value());
                    body.insert(QStringLiteral("display_name"), nameValue.value());
                    body.insert(QStringLiteral("auth_key"), encodeBase64(keys.file.value().kauth));
                    body.insert(QStringLiteral("password_key"), encodeBase64(keys.password.value()));
                    body.insert(QStringLiteral("nonce"), prep.nonce);
                    body.insert(QStringLiteral("captcha_ticket"), captchaTicket);
                    // devices.id is globally unique. A registration creates a
                    // new device row, so it must never reuse the id persisted
                    // for another account on this installation.
                    m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    body.insert(QStringLiteral("device"), deviceBody(m_deviceId));

                    request("POST", QStringLiteral("/api/v1/auth/register"), body, false,
                            [this, material, prep, keys, done](Result<QJsonObject> registered) {
                        if (registered.isError()) {
                            done(Result<RegistrationResult>::error(registered.error()));
                            return;
                        }
                        bool fallback = false;
                        const Result<QString> saved = m_signIns.save(material, &fallback);
                        if (saved.isError()) {
                            done(Result<RegistrationResult>::error(saved.error()));
                            return;
                        }
                        handleSession(registered.value());
                        m_saltPw = prep.saltPw;
                        m_fileKeys = keys.file.value();
                        m_signInPath = saved.value();

                        RegistrationResult result;
                        result.material = material;
                        result.filePath = saved.value();
                        result.usedFallbackDirectory = fallback;
                        result.session = m_session;
                        done(Result<RegistrationResult>::ok(result));
                    });
                });
        }, false);
    });
}

void AuthService::login(const QString &login, const QString &password,
                        const QString &signInPath, const QString &captchaTicket,
                        Done<AuthSession> done)
{
    if (!done) {
        return;
    }
    if (password.isEmpty()) {
        done(Result<AuthSession>::error(QStringLiteral("password.required")));
        return;
    }
    if (login.trimmed().isEmpty()) {
        done(Result<AuthSession>::error(QStringLiteral("login.required")));
        return;
    }
    if (signInPath.trimmed().isEmpty()) {
        done(Result<AuthSession>::error(QStringLiteral("signin.missing")));
        return;
    }
    const Result<crypto::NormalizedSignIn> parsed = m_signIns.read(signInPath);
    if (parsed.isError()) {
        done(Result<AuthSession>::error(parsed.error()));
        return;
    }
    const QString value = login.trimmed();

    QJsonObject prepareBody;
    prepareBody.insert(QStringLiteral("login"), value);
    prepareBody.insert(QStringLiteral("purpose"), QStringLiteral("login"));
    request("POST", QStringLiteral("/api/v1/auth/prepare"), prepareBody, false,
            [this, value, password, signInPath, parsed, captchaTicket, done](
                Result<QJsonObject> prepared) {
        if (prepared.isError()) {
            done(Result<AuthSession>::error(prepared.error()));
            return;
        }
        const QJsonObject root = prepared.value();
        PreparedAuth prep;
        prep.saltFile = decodeBase64Any(root.value(QStringLiteral("salt_file")).toString());
        prep.saltPw = decodeBase64Any(root.value(QStringLiteral("salt_pw")).toString());
        prep.nonce = root.value(QStringLiteral("nonce")).toString();
        const QJsonObject captcha = root.value(QStringLiteral("captcha")).toObject();
        prep.captcha.provider = captcha.value(QStringLiteral("provider")).toString();
        prep.captcha.required = captcha.value(QStringLiteral("required")).toBool();
        prep.captcha.siteKey = captcha.value(QStringLiteral("site_key")).toString();
        if (prep.saltFile.size() != crypto::Kdf::kSaltBytes
            || prep.saltPw.size() != crypto::Kdf::kSaltBytes || prep.nonce.isEmpty()) {
            done(Result<AuthSession>::error(QStringLiteral("protocol")));
            return;
        }
        // Login must work without a ticket even when the server has a captcha
        // provider configured: the server demands one only after repeated
        // failures and answers captcha_required, which the UI then satisfies.
        // (Registration requires the captcha before this point.)

        auto derive = [parsed, prep, password] {
            return DerivedFileKeys(
                crypto::Kdf::deriveFileKeys(parsed.value().normalized, prep.saltFile),
                crypto::Kdf::derivePasswordKey(password, prep.saltPw));
        };
        QtConcurrent::run(derive).then(
            this, [this, value, signInPath, prep, captchaTicket, done](DerivedFileKeys keys) {
                if (keys.file.isError()) {
                    done(Result<AuthSession>::error(keys.file.error()));
                    return;
                }
                if (keys.password.isError()) {
                    done(Result<AuthSession>::error(keys.password.error()));
                    return;
                }
                QJsonObject body;
                body.insert(QStringLiteral("login"), value);
                body.insert(QStringLiteral("signin_key"), encodeBase64(keys.file.value().kauth));
                body.insert(QStringLiteral("password_key"), encodeBase64(keys.password.value()));
                body.insert(QStringLiteral("nonce"), prep.nonce);
                if (!captchaTicket.isEmpty()) {
                    body.insert(QStringLiteral("captcha_ticket"), captchaTicket);
                }
                body.insert(QStringLiteral("device"), deviceBody(storedDeviceId()));

                request("POST", QStringLiteral("/api/v1/auth/login"), body, false,
                        [this, prep, keys, signInPath, done](Result<QJsonObject> logged) {
                    if (logged.isError()) {
                        done(Result<AuthSession>::error(logged.error()));
                        return;
                    }
                    handleSession(logged.value());
                    m_saltPw = prep.saltPw;
                    m_fileKeys = keys.file.value();
                    m_signInPath = signInPath;
                    done(Result<AuthSession>::ok(m_session));
                });
            });
    });
}

void AuthService::listDevices(Done<QList<AuthDevice>> done)
{
    if (!done) {
        return;
    }
    request("GET", QStringLiteral("/api/v1/devices"), {}, true,
            [done](Result<QJsonObject> result) {
        if (result.isError()) {
            done(Result<QList<AuthDevice>>::error(result.error()));
            return;
        }
        QList<AuthDevice> devices;
        const QJsonArray array = result.value().value(QStringLiteral("devices")).toArray();
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            AuthDevice device;
            device.deviceId = object.value(QStringLiteral("id")).toString();
            device.name = object.value(QStringLiteral("name")).toString();
            device.platform = object.value(QStringLiteral("platform")).toString();
            device.lastSeenAt = static_cast<qint64>(
                object.value(QStringLiteral("last_seen_at")).toDouble());
            device.current = object.value(QStringLiteral("current")).toBool();
            devices.append(device);
        }
        done(Result<QList<AuthDevice>>::ok(devices));
    });
}

void AuthService::revokeDevice(const QString &deviceId, Done<bool> done)
{
    if (!done) {
        return;
    }
    const QString path = QStringLiteral("/api/v1/devices/")
        + QString::fromLatin1(QUrl::toPercentEncoding(deviceId));
    request("DELETE", path, {}, true, [done](Result<QJsonObject> result) {
        if (result.isError()) {
            done(Result<bool>::error(result.error()));
            return;
        }
        done(Result<bool>::ok(true));
    });
}

void AuthService::changePassword(const QString &oldPassword, const QString &newPassword,
                                 Done<bool> done)
{
    if (!done) {
        return;
    }
    if (!hasSession()) {
        done(Result<bool>::error(QStringLiteral("unauthorized")));
        return;
    }
    if (m_saltPw.size() != crypto::Kdf::kSaltBytes) {
        done(Result<bool>::error(QStringLiteral("session.incomplete")));
        return;
    }
    const Result<QString> valid = validatePassword(newPassword, m_session.login,
                                                   m_session.displayName);
    if (valid.isError()) {
        done(Result<bool>::error(valid.error()));
        return;
    }

    const QByteArray salt = m_saltPw;
    auto derive = [oldPassword, newPassword, salt] {
        return DerivedPasswordKeys(
            crypto::Kdf::derivePasswordKey(oldPassword, salt),
            crypto::Kdf::derivePasswordKey(newPassword, salt));
    };
    QtConcurrent::run(derive).then(
        this, [this, done](DerivedPasswordKeys keys) {
            if (keys.oldKey.isError()) {
                done(Result<bool>::error(keys.oldKey.error()));
                return;
            }
            if (keys.newKey.isError()) {
                done(Result<bool>::error(keys.newKey.error()));
                return;
            }
            QJsonObject body;
            body.insert(QStringLiteral("old_password_key"), encodeBase64(keys.oldKey.value()));
            body.insert(QStringLiteral("new_password_key"), encodeBase64(keys.newKey.value()));
            request("POST", QStringLiteral("/api/v1/auth/change_password"), body, true,
                    [done](Result<QJsonObject> result) {
                if (result.isError()) {
                    done(Result<bool>::error(result.error()));
                    return;
                }
                done(Result<bool>::ok(true));
            });
        });
}

void AuthService::reissueSignIn(Done<ReissueResult> done)
{
    if (!done) {
        return;
    }
    if (!hasSession() || m_fileKeys.kenc.isEmpty()) {
        done(Result<ReissueResult>::error(QStringLiteral("unauthorized")));
        return;
    }
    QJsonObject prepareBody;
    prepareBody.insert(QStringLiteral("login"), m_session.login);
    prepareBody.insert(QStringLiteral("purpose"), QStringLiteral("reissue"));
    request("POST", QStringLiteral("/api/v1/auth/prepare"), prepareBody, true,
            [this, done](Result<QJsonObject> prepared) {
        if (prepared.isError()) {
            done(Result<ReissueResult>::error(prepared.error()));
            return;
        }
        const QJsonObject root = prepared.value();
        const QByteArray newSaltFile =
            decodeBase64Any(root.value(QStringLiteral("salt_file")).toString());
        const QString nonce = root.value(QStringLiteral("nonce")).toString();
        if (newSaltFile.size() != crypto::Kdf::kSaltBytes || nonce.isEmpty()) {
            done(Result<ReissueResult>::error(QStringLiteral("protocol")));
            return;
        }
        const Result<crypto::SignInMaterial> created = m_signIns.create();
        if (created.isError()) {
            done(Result<ReissueResult>::error(created.error()));
            return;
        }
        const crypto::SignInMaterial material = created.value();

        auto derive = [material, newSaltFile] {
            return crypto::Kdf::deriveFileKeys(material.parsed.normalized, newSaltFile);
        };
        QtConcurrent::run(derive).then(
            this, [this, material, nonce, done](Result<crypto::FileKeys> newKeys) {
                if (newKeys.isError()) {
                    done(Result<ReissueResult>::error(newKeys.error()));
                    return;
                }
                const QByteArray oldKenc = m_fileKeys.kenc;
                const QByteArray newKenc = newKeys.value().kenc;
                auto rekey = [this, oldKenc, newKenc]() -> Result<bool> {
                    return m_rekey ? m_rekey(oldKenc, newKenc)
                                   : rekeyLocal(oldKenc, newKenc);
                };
                QtConcurrent::run(rekey).then(
                    this, [this, material, nonce, newKeys, oldKenc, newKenc, done](
                              Result<bool> rekeyed) {
                    if (rekeyed.isError()) {
                        done(Result<ReissueResult>::error(rekeyed.error()));
                        return;
                    }

                    const QString target = m_signInPath.isEmpty()
                        ? SignInService::uniquePath(
                              SignInService::defaultDownloadsDirectory())
                        : m_signInPath;

                    QByteArray previous;
                    bool hadPrevious = false;
                    {
                        QFile existing(target);
                        if (existing.exists()) {
                            if (!existing.open(QIODevice::ReadOnly)) {
                                auto rollbackOnly = [this, oldKenc, newKenc] {
                                    return m_rekey ? m_rekey(newKenc, oldKenc)
                                                   : rekeyLocal(newKenc, oldKenc);
                                };
                                QtConcurrent::run(rollbackOnly).then(
                                    this, [done](Result<bool> rolled) {
                                        done(Result<ReissueResult>::error(
                                            rolled.isOk()
                                                ? QStringLiteral("reissue.save")
                                                : QStringLiteral("rekey.rollback")));
                                    });
                                return;
                            }
                            previous = existing.readAll();
                            hadPrevious = true;
                        }
                    }

                    auto rollbackLocal = [this, oldKenc, newKenc]() -> Result<bool> {
                        return m_rekey ? m_rekey(newKenc, oldKenc)
                                       : rekeyLocal(newKenc, oldKenc);
                    };
                    auto failWithRollback = [this, target, previous, hadPrevious,
                                             rollbackLocal, done](const QString &code) {
                        const bool restored = hadPrevious
                            ? m_signIns.writeBytes(target, previous).isOk()
                            : QFile::remove(target);
                        QtConcurrent::run(rollbackLocal).then(
                            this, [code, restored, done](Result<bool> rolled) {
                                done(Result<ReissueResult>::error(
                                    restored && rolled.isOk()
                                        ? code
                                        : QStringLiteral("rekey.rollback")));
                            });
                    };

                    // The new file is committed before the server rotates the
                    // verifier: after that point the old file cannot be
                    // restored server-side, so it must already exist locally.
                    const Result<QString> written = m_signIns.saveTo(material, target);
                    if (written.isError()) {
                        QtConcurrent::run(rollbackLocal).then(
                            this, [written, done](Result<bool> rolled) {
                                done(Result<ReissueResult>::error(
                                    rolled.isOk() ? written.error()
                                                  : QStringLiteral("rekey.rollback")));
                            });
                        return;
                    }

                    QJsonObject body;
                    body.insert(QStringLiteral("auth_key"),
                                encodeBase64(newKeys.value().kauth));
                    body.insert(QStringLiteral("nonce"), nonce);
                    request("POST", QStringLiteral("/api/v1/auth/reissue"), body, true,
                            [this, material, target, newKeys, failWithRollback, done](
                                Result<QJsonObject> reissued) {
                        if (reissued.isError()) {
                            failWithRollback(reissued.error());
                            return;
                        }
                        m_fileKeys = newKeys.value();
                        m_signInPath = target;
                        ReissueResult result;
                        result.material = material;
                        result.filePath = target;
                        done(Result<ReissueResult>::ok(result));
                    });
                });
            });
    });
}

void AuthService::deleteAccount(const QString &typedLogin, const QString &password,
                                bool confirmed, Done<bool> done)
{
    if (!done) {
        return;
    }
    if (!hasSession()) {
        done(Result<bool>::error(QStringLiteral("unauthorized")));
        return;
    }
    if (!confirmed) {
        done(Result<bool>::error(QStringLiteral("delete.unconfirmed")));
        return;
    }
    if (typedLogin.trimmed().toLower() != m_session.login.toLower()) {
        done(Result<bool>::error(QStringLiteral("delete.login_mismatch")));
        return;
    }
    if (password.isEmpty() || m_saltPw.size() != crypto::Kdf::kSaltBytes) {
        done(Result<bool>::error(QStringLiteral("password.required")));
        return;
    }

    const QByteArray salt = m_saltPw;
    QtConcurrent::run([password, salt] {
        return crypto::Kdf::derivePasswordKey(password, salt);
    }).then(this, [this, done](Result<QByteArray> passwordKey) {
        if (passwordKey.isError()) {
            done(Result<bool>::error(passwordKey.error()));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("login"), m_session.login);
        body.insert(QStringLiteral("password_key"), encodeBase64(passwordKey.value()));
        request("POST", QStringLiteral("/api/v1/auth/delete_account"), body, true,
                [this, done](Result<QJsonObject> result) {
            if (result.isError()) {
                done(Result<bool>::error(result.error()));
                return;
            }
            if (!m_signInPath.isEmpty()) {
                QFile::remove(m_signInPath);
            }
            removeLocalSecrets(true, true);
            clearSession();
            done(Result<bool>::ok(true));
        });
    });
}

void AuthService::logout(Done<bool> done)
{
    if (!done) {
        return;
    }
    if (!hasSession()) {
        removeLocalSecrets(false, false);
        clearSession();
        done(Result<bool>::ok(true));
        return;
    }
    QJsonObject body;
    if (!m_refreshToken.isEmpty()) {
        body.insert(QStringLiteral("refresh_token"), m_refreshToken);
    }
    request("POST", QStringLiteral("/api/v1/auth/logout"), body, true,
            [this, done](Result<QJsonObject> result) {
        // Local keys never outlive the session, even if the server call failed.
        removeLocalSecrets(false, false);
        clearSession();
        if (result.isError() && result.error() != QStringLiteral("session_revoked")
            && result.error() != QStringLiteral("unauthorized")) {
            done(Result<bool>::error(result.error()));
            return;
        }
        done(Result<bool>::ok(true));
    });
}

void AuthService::removeLocalSecrets(bool removeDatabase, bool removeMedia)
{
    const QString dir = m_databasePath.isEmpty()
        ? Paths::dataDir()
        : QFileInfo(m_databasePath).absolutePath();
    const QString keysPath = m_keysPath.isEmpty()
        ? QDir(dir).filePath(QStringLiteral("keys.bin"))
        : m_keysPath;
    const QString databasePath = m_databasePath.isEmpty()
        ? QDir(dir).filePath(QStringLiteral("mini.db"))
        : m_databasePath;

    QFile::remove(keysPath);
    QFile::remove(QDir(dir).filePath(QStringLiteral("session.bin")));
    QFile::remove(QDir(dir).filePath(QStringLiteral("db.wrap")));
    if (removeDatabase) {
        QFile::remove(databasePath);
        QFile::remove(databasePath + QStringLiteral("-wal"));
        QFile::remove(databasePath + QStringLiteral("-shm"));
    }
    if (removeMedia) {
        QDir(Paths::mediaDir()).removeRecursively();
    }
}

void AuthService::clearSession()
{
    m_session = AuthSession();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_login.clear();
    m_signInPath.clear();
    m_saltPw.fill('\0');
    m_saltPw.clear();
    for (QByteArray *key : {&m_fileKeys.kfile, &m_fileKeys.kauth, &m_fileKeys.kenc,
                            &m_fileKeys.kmac}) {
        if (!key->isEmpty()) {
            sodium_memzero(key->data(), static_cast<size_t>(key->size()));
            key->clear();
        }
    }
}

} // namespace mini::services
