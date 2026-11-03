#include "net/ApiClient.h"

#include "net/TlsPin.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>

namespace mini::net {

namespace {

QJsonObject parseObjectOr(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object();
}

bool parseObject(const QByteArray &data, QJsonObject *object, ApiError *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = ApiError::protocol(QStringLiteral("response_is_not_json_object"));
        return false;
    }
    *object = document.object();
    return true;
}

qint64 toInt64(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

QByteArray fromBase64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

QString toBase64(const QByteArray &value)
{
    return QString::fromLatin1(value.toBase64());
}

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QByteArray firstBytes(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString();
        if (!value.isEmpty()) {
            return QByteArray::fromBase64(value.toLatin1());
        }
    }
    return {};
}

DeviceInfo parseDevice(const QJsonObject &object)
{
    DeviceInfo device;
    device.deviceId = firstString(object, {"device_id", "id"});
    device.name = firstString(object, {"name", "device_name"});
    device.ip = firstString(object, {"ip", "ip_address"});
    device.lastActiveAt = toInt64(object.value(QStringLiteral("last_active_at")));
    return device;
}

AccountProfile parseProfile(const QJsonObject &object)
{
    AccountProfile profile;
    profile.userId = firstString(object, {"user_id", "userId", "id"});
    profile.login = firstString(object, {"login"});
    profile.displayName = firstString(object, {"display_name", "displayName", "name"});
    profile.bio = firstString(object, {"bio"});
    profile.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    profile.online = object.value(QStringLiteral("online")).toBool();
    profile.lastActiveAt = toInt64(object.value(QStringLiteral("last_active_at")));

    const QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue &value : devices) {
        if (value.isObject()) {
            profile.devices.append(parseDevice(value.toObject()));
        }
    }
    return profile;
}

AuthSession parseSession(const QJsonObject &root){
    AuthSession session;
    session.accessToken = firstString(root, {"access_token", "accessToken"});
    session.refreshToken = firstString(root, {"refresh_token", "refreshToken"});
    session.deviceId = firstString(root, {"device_id"});
    session.accessExpiresAt = toInt64(root.value(QStringLiteral("access_expires_at")));
    session.refreshExpiresAt = toInt64(root.value(QStringLiteral("refresh_expires_at")));

    QJsonObject user = root.value(QStringLiteral("user")).toObject();
    if (user.isEmpty()) {
        user = root;
    }
    session.user = parseProfile(user);
    return session;
}

QJsonObject envelopeToJson(const Envelope &envelope)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), envelope.kind);
    object.insert(QStringLiteral("client_msg_id"), envelope.clientMsgId);
    object.insert(QStringLiteral("chat_id"), envelope.chatId);
    object.insert(QStringLiteral("ciphertext_b64"), toBase64(envelope.ciphertext));
    object.insert(QStringLiteral("header"), envelope.header);
    return object;
}

Envelope envelopeFromJson(const QJsonObject &object)
{
    Envelope envelope;
    envelope.kind = firstString(object, {"kind"});
    envelope.clientMsgId = firstString(object, {"client_msg_id", "clientMsgId"});
    envelope.chatId = firstString(object, {"chat_id", "chatId"});
    envelope.ciphertext = firstBytes(object, {"ciphertext_b64", "ciphertext"});
    envelope.header = object.value(QStringLiteral("header")).toObject();
    return envelope;
}

QStringList stringList(const QJsonArray &array)
{    QStringList values;
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            values.append(value.toString());
        } else if (value.isObject()) {
            const QString id = firstString(value.toObject(), {"client_msg_id", "mail_id", "id"});
            if (!id.isEmpty()) {
                values.append(id);
            }
        }
    }
    return values;
}

} // namespace

ApiClient::ApiClient(const Endpoint &endpoint, TlsPin *pin, QObject *parent)
    : ApiClient(endpoint, pin, new QNetworkAccessManager(), parent)
{
    m_ownsNetwork = true;
    m_network->setParent(this);
}

ApiClient::ApiClient(const Endpoint &endpoint, TlsPin *pin,
                     QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_endpoint(endpoint)
    , m_pin(pin)
    , m_network(network ? network : new QNetworkAccessManager())
{
    if (!network) {
        m_ownsNetwork = true;
        m_network->setParent(this);
    }

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

    if (m_pin) {
        connect(m_pin, &TlsPin::mismatchDetected, this,
                &ApiClient::certificateMismatch);
        connect(m_pin, &TlsPin::pinUnavailable, this,
                [this](const QString &reason, const QString &expected) {
                    emit certificateWarning(reason, expected);
                });
    }
}

ApiClient::~ApiClient()
{
    if (m_network && !m_ownsNetwork) {
        m_network->disconnect(this);
    }
}

void ApiClient::setProtocolVersion(int version)
{
    m_protocolVersion = version;
}

void ApiClient::send(const QByteArray &method, const QString &path,
                     const QJsonObject &body, bool authenticated, RawCallback callback)
{
    if (!callback) {
        return;
    }
    if (!m_endpoint.isValid()) {
        callback(ApiResult<QByteArray>::fail(
            ApiError::protocol(QStringLiteral("invalid_endpoint"))));
        return;
    }

    const QUrl url = m_endpoint.httpUrl(path);
    if (!url.isValid()) {
        callback(ApiResult<QByteArray>::fail(
            ApiError::protocol(QStringLiteral("invalid_url"))));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "MINI-Messenger");
    request.setRawHeader("X-Protocol-Version", QByteArray::number(m_protocolVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_transferTimeoutMs);

    if (authenticated && !m_accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    }
    if (m_endpoint.isSecure()) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setProtocol(QSsl::TlsV1_3);
        request.setSslConfiguration(ssl);
    }

    const QByteArray payload =
        method == "GET" ? QByteArray()
                        : QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_network->get(request);
    } else if (method == "POST") {
        reply = m_network->post(request, payload);
    } else {
        reply = m_network->sendCustomRequest(request, method, payload);
    }

    // encrypted() fires after the TLS handshake and before the HTTP request is
    // sent. A rejected pin must abort here so K_auth/PW_KEY/Bearer never leave.
    connect(reply, &QNetworkReply::encrypted, this, [this, reply]() {
        const QSslCertificate certificate =
            reply->sslConfiguration().peerCertificate();
        const TlsPin::Outcome outcome =
            m_pin ? m_pin->check(certificate) : TlsPin::Outcome::NotConfigured;

        switch (outcome) {
        case TlsPin::Outcome::Match:
            return;
        case TlsPin::Outcome::Mismatch:
            if (m_pin && m_pin->shouldIgnore(certificate)) {
                return;
            }
            reply->setProperty("mini_tls_rejected",
                               QStringLiteral("fingerprint_mismatch"));
            reply->abort();
            return;
        case TlsPin::Outcome::NotConfigured:
        case TlsPin::Outcome::Invalid:
            // Without a valid pin the certificate cannot be authenticated, so
            // the request (and any secret it carries) must not be sent.
            if (m_pin) {
                m_pin->shouldIgnore(certificate);
            }
            emit certificateWarning(
                outcome == TlsPin::Outcome::Invalid ? QStringLiteral("invalid_fingerprint")
                                                    : QStringLiteral("not_configured"),
                m_pin ? m_pin->expected() : QString());
            reply->setProperty("mini_tls_rejected", QStringLiteral("tls_pin_required"));
            reply->abort();
            return;
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        const QString tlsRejected = reply->property("mini_tls_rejected").toString();
        if (!tlsRejected.isEmpty()) {
            callback(ApiResult<QByteArray>::fail(ApiError::tls(tlsRejected)));
            return;
        }
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();

        if (status == 0 && networkError != QNetworkReply::NoError) {
            if (networkError == QNetworkReply::TimeoutError
                || networkError == QNetworkReply::OperationCanceledError) {
                callback(ApiResult<QByteArray>::fail(ApiError::timeout()));
            } else if (networkError == QNetworkReply::SslHandshakeFailedError) {
                callback(ApiResult<QByteArray>::fail(
                    ApiError::tls(QStringLiteral("ssl_handshake_failed"))));
            } else {
                callback(ApiResult<QByteArray>::fail(
                    ApiError::network(reply->errorString())));
            }
            return;
        }

        if (status >= 400) {
            const ApiError error =
                ApiError::fromStatus(status, data, reply->rawHeader("Retry-After"));
            if (status == 401) {
                emit authenticationRequired();
            }
            callback(ApiResult<QByteArray>::fail(error));
            return;
        }

        if (networkError != QNetworkReply::NoError) {
            callback(ApiResult<QByteArray>::fail(
                ApiError::network(reply->errorString())));
            return;
        }

        const ApiError tls = tlsErrorFor(reply);
        if (tls.isError()) {
            callback(ApiResult<QByteArray>::fail(tls));
            return;
        }

        if (!data.isEmpty()) {
            const QJsonObject root = parseObjectOr(data);
            const QJsonValue version = root.value(QStringLiteral("protocol_version"));
            if (!version.isUndefined() && version.toInt() != m_protocolVersion) {
                emit protocolVersionMismatch(version.toInt());
                callback(ApiResult<QByteArray>::fail(
                    ApiError::protocol(QStringLiteral("protocol_version_mismatch"))));
                return;
            }
        }

        callback(ApiResult<QByteArray>::ok(data));
    });
}

ApiError ApiClient::tlsErrorFor(QNetworkReply *reply) const
{
    if (!m_endpoint.isSecure() || !m_pin) {
        return {};
    }
    const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
    if (certificate.isNull()) {
        return ApiError::tls(QStringLiteral("no_peer_certificate"));
    }
    switch (m_pin->check(certificate)) {
    case TlsPin::Outcome::Match:
    case TlsPin::Outcome::NotConfigured:
    case TlsPin::Outcome::Invalid:
        // Missing/malformed pin is a warning for HTTPS; it was reported in
        // the encrypted() handler before any request bytes were sent.
        return {};
    case TlsPin::Outcome::Mismatch:
        return ApiError::tls(QStringLiteral("fingerprint_mismatch"));
    }
    return ApiError::tls(QStringLiteral("unknown"));
}

void ApiClient::prepare(const QString &login, Callback<AuthPrepare> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), login);
    send("POST", QStringLiteral("/api/v1/auth/prepare"), body, false,
         [login, callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AuthPrepare>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AuthPrepare>::fail(error));
                 return;
             }
             AuthPrepare prepared;
             prepared.login = firstString(root, {"login"});
             if (prepared.login.isEmpty()) {
                 prepared.login = login;
             }
             prepared.saltFile = firstBytes(root, {"salt_file", "salt_file_b64"});
             prepared.saltPw = firstBytes(root, {"salt_pw", "salt_pw_b64"});
             const QJsonObject kdf = root.value(QStringLiteral("kdf")).toObject();
             if (!kdf.isEmpty()) {
                 prepared.kdf = firstString(kdf, {"algo", "algorithm"});
                 prepared.memoryKiB = kdf.value(QStringLiteral("m")).toInt(prepared.memoryKiB);
                 prepared.iterations = kdf.value(QStringLiteral("t")).toInt(prepared.iterations);
                 prepared.parallelism = kdf.value(QStringLiteral("p")).toInt(prepared.parallelism);
                 prepared.keyLength = kdf.value(QStringLiteral("len")).toInt(prepared.keyLength);
             }
             callback(ApiResult<AuthPrepare>::ok(prepared));
         });
}

void ApiClient::checkLogin(const QString &login, Callback<LoginAvailability> callback)
{
    const QString path = QStringLiteral("/api/v1/auth/check_login?login=")
        + QString::fromLatin1(QUrl::toPercentEncoding(login));
    send("GET", path, {}, false, [login, callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<LoginAvailability>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<LoginAvailability>::fail(error));
            return;
        }
        LoginAvailability availability;
        availability.login = firstString(root, {"login"});
        if (availability.login.isEmpty()) {
            availability.login = login;
        }
        availability.available = root.value(QStringLiteral("available")).toBool();
        availability.reason = firstString(root, {"reason", "code"});
        callback(ApiResult<LoginAvailability>::ok(availability));
    });
}

void ApiClient::registerAccount(const RegisterRequest &request,
                               Callback<AuthSession> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), request.login);
    body.insert(QStringLiteral("display_name"), request.displayName);
    body.insert(QStringLiteral("auth_key"), toBase64(request.authKey));
    body.insert(QStringLiteral("password_key"), toBase64(request.passwordKey));
    body.insert(QStringLiteral("nonce"), request.nonce);
    body.insert(QStringLiteral("captcha_ticket"), request.captchaTicket);
    QJsonObject device;
    device.insert(QStringLiteral("device_id"), request.deviceId);
    device.insert(QStringLiteral("name"), request.deviceName);
    device.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    body.insert(QStringLiteral("device"), device);

    send("POST", QStringLiteral("/api/v1/auth/register"), body, false,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AuthSession>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AuthSession>::fail(error));
                 return;
             }
             callback(ApiResult<AuthSession>::ok(parseSession(root)));
         });
}

void ApiClient::login(const LoginRequest &request, Callback<AuthSession> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), request.login);
    body.insert(QStringLiteral("signin_key"), toBase64(request.authKey));
    body.insert(QStringLiteral("password_key"), toBase64(request.passwordKey));
    body.insert(QStringLiteral("nonce"), request.nonce);
    QJsonObject device;
    device.insert(QStringLiteral("device_id"), request.deviceId);
    device.insert(QStringLiteral("name"), request.deviceName);
    device.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    body.insert(QStringLiteral("device"), device);

    send("POST", QStringLiteral("/api/v1/auth/login"), body, false,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AuthSession>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AuthSession>::fail(error));
                 return;
             }
             callback(ApiResult<AuthSession>::ok(parseSession(root)));
         });
}

void ApiClient::sendEnvelopes(const QList<Envelope> &envelopes,
                              Callback<SendResult> callback)
{
    QJsonArray array;
    for (const Envelope &envelope : envelopes) {
        array.append(envelopeToJson(envelope));
    }
    QJsonObject body;
    body.insert(QStringLiteral("envelopes"), array);

    send("POST", QStringLiteral("/api/v1/mail/send"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<SendResult>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<SendResult>::fail(error));
                 return;
             }
             SendResult result;
             result.accepted = stringList(root.value(QStringLiteral("accepted")).toArray());
             result.rejected = stringList(root.value(QStringLiteral("rejected")).toArray());
             callback(ApiResult<SendResult>::ok(result));
         });
}

void ApiClient::pullMail(const QString &deviceId, const QString &cursor,
                         Callback<MailBatch> callback)
{
    QString path = QStringLiteral("/api/v1/mail/pull?device=")
        + QString::fromLatin1(QUrl::toPercentEncoding(deviceId));
    if (!cursor.isEmpty()) {
        path += QStringLiteral("&cursor=")
            + QString::fromLatin1(QUrl::toPercentEncoding(cursor));
    }

    send("GET", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<MailBatch>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<MailBatch>::fail(error));
            return;
        }
        MailBatch batch;
        batch.cursor = firstString(root, {"cursor", "next_cursor"});
        batch.hasMore = root.value(QStringLiteral("has_more")).toBool();
        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject itemObject = value.toObject();
            MailItem item;
            item.mailId = firstString(itemObject, {"mail_id", "id"});
            item.receivedAt = toInt64(itemObject.value(QStringLiteral("received_at")));
            QJsonObject envelopeObject = itemObject.value(QStringLiteral("envelope")).toObject();
            if (envelopeObject.isEmpty()) {
                envelopeObject = itemObject;
            }
            item.envelope = envelopeFromJson(envelopeObject);
            batch.items.append(item);
        }
        callback(ApiResult<MailBatch>::ok(batch));
    });
}

void ApiClient::ackMail(const QStringList &mailIds, Callback<AckResult> callback)
{
    QJsonArray ids;
    for (const QString &id : mailIds) {
        ids.append(id);
    }
    QJsonObject body;
    body.insert(QStringLiteral("ids"), ids);

    send("POST", QStringLiteral("/api/v1/mail/ack"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AckResult>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AckResult>::fail(error));
                 return;
             }
             AckResult result;
             result.acked = stringList(root.value(QStringLiteral("acked")).toArray());
             callback(ApiResult<AckResult>::ok(result));
         });
}

void ApiClient::getMe(Callback<AccountProfile> callback)
{
    send("GET", QStringLiteral("/api/v1/me"), {}, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AccountProfile>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AccountProfile>::fail(error));
                 return;
             }
             QJsonObject user = root.value(QStringLiteral("user")).toObject();
             if (user.isEmpty()) {
                 user = root;
             }
             callback(ApiResult<AccountProfile>::ok(parseProfile(user)));
         });
}

void ApiClient::patchMe(const ProfilePatch &patch, Callback<AccountProfile> callback)
{
    QJsonObject body;
    if (patch.displayName.has_value()) {
        body.insert(QStringLiteral("display_name"), *patch.displayName);
    }
    if (patch.bio.has_value()) {
        body.insert(QStringLiteral("bio"), *patch.bio);
    }

    send("PATCH", QStringLiteral("/api/v1/me"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<AccountProfile>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<AccountProfile>::fail(error));
                 return;
             }
             QJsonObject user = root.value(QStringLiteral("user")).toObject();
             if (user.isEmpty()) {
                 user = root;
             }
             callback(ApiResult<AccountProfile>::ok(parseProfile(user)));
         });
}

void ApiClient::changePassword(const ChangePasswordRequest &request,
                               Callback<Empty> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("old_password_key"), toBase64(request.oldPasswordKey));
    body.insert(QStringLiteral("new_password_key"), toBase64(request.newPasswordKey));

    send("POST", QStringLiteral("/api/v1/auth/change_password"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<Empty>::fail(raw.error()));
                 return;
             }
             callback(ApiResult<Empty>::ok(Empty()));
         });
}

void ApiClient::reissueSignIn(const ReissueRequest &request,
                              Callback<ReissueResult> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), request.login);
    body.insert(QStringLiteral("nonce"), request.nonce);
    body.insert(QStringLiteral("new_k_auth"), toBase64(request.newAuthKey));
    body.insert(QStringLiteral("new_pw_key"), toBase64(request.newPasswordKey));

    send("POST", QStringLiteral("/api/v1/auth/reissue"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<ReissueResult>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<ReissueResult>::fail(error));
                 return;
             }
             ReissueResult result;
             result.saltFile = firstBytes(root, {"salt_file", "salt_file_b64"});
             result.saltPw = firstBytes(root, {"salt_pw", "salt_pw_b64"});
             const QJsonObject kdf = root.value(QStringLiteral("kdf")).toObject();
             if (!kdf.isEmpty()) {
                 result.kdf = firstString(kdf, {"algo", "algorithm"});
                 result.memoryKiB = kdf.value(QStringLiteral("m")).toInt(result.memoryKiB);
                 result.iterations = kdf.value(QStringLiteral("t")).toInt(result.iterations);
                 result.parallelism = kdf.value(QStringLiteral("p")).toInt(result.parallelism);
                 result.keyLength = kdf.value(QStringLiteral("len")).toInt(result.keyLength);
             }
             result.session = parseSession(root);
             callback(ApiResult<ReissueResult>::ok(result));
         });
}

void ApiClient::deleteAccount(const DeleteAccountRequest &request,
                              Callback<Empty> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), request.login);
    body.insert(QStringLiteral("password_key"), toBase64(request.passwordKey));

    send("POST", QStringLiteral("/api/v1/auth/delete_account"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<Empty>::fail(raw.error()));
                 return;
             }
             callback(ApiResult<Empty>::ok(Empty()));
         });
}

void ApiClient::requestWsTicket(Callback<WsTicket> callback)
{
    QJsonObject body;
    if (!m_deviceId.isEmpty()) {
        body.insert(QStringLiteral("device_id"), m_deviceId);
    }

    send("POST", QStringLiteral("/api/v1/ws/ticket"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<WsTicket>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<WsTicket>::fail(error));
                 return;
             }
             WsTicket ticket;
             ticket.ticket = firstString(root, {"ticket"});
             ticket.expiresIn = root.value(QStringLiteral("expires_in")).toInt(300);
             if (ticket.ticket.isEmpty()) {
                 callback(ApiResult<WsTicket>::fail(
                     ApiError::protocol(QStringLiteral("ticket_missing"))));
                 return;
             }
             callback(ApiResult<WsTicket>::ok(ticket));
         });
}

void ApiClient::putPrekeys(const PeerKeys &keys, Callback<Empty> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("identity_key"), toBase64(keys.identityKey));
    body.insert(QStringLiteral("signed_prekey"), toBase64(keys.signedPrekey));
    body.insert(QStringLiteral("signed_prekey_signature"),
                toBase64(keys.signedPrekeySignature));
    if (keys.signedPrekeyId != 0) {
        body.insert(QStringLiteral("signed_prekey_id"),
                    static_cast<qint64>(keys.signedPrekeyId));
    }
    QJsonArray prekeys;
    for (const auto &entry : keys.oneTimePrekeys) {
        if (entry.first == 0 || entry.second.size() != 32) {
            continue;
        }
        prekeys.append(QJsonObject{{QStringLiteral("id"), static_cast<qint64>(entry.first)},
                                   {QStringLiteral("key"), toBase64(entry.second)}});
    }
    body.insert(QStringLiteral("one_time_prekeys"), prekeys);

    send("PUT", QStringLiteral("/api/v1/mail/keys"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<Empty>::fail(raw.error()));
                 return;
             }
             callback(ApiResult<Empty>::ok(Empty()));
         });
}

void ApiClient::fetchPeerKeys(const QString &login, Callback<PeerKeys> callback)
{
    const QString path = QStringLiteral("/api/v1/mail/keys/")
        + QString::fromLatin1(QUrl::toPercentEncoding(login));
    send("GET", path, {}, true, [login, callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<PeerKeys>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<PeerKeys>::fail(error));
            return;
        }
        PeerKeys keys;
        keys.login = firstString(root, {"login"});
        if (keys.login.isEmpty()) {
            keys.login = login;
        }
        keys.deviceId = firstString(root, {"device_id"});
        keys.identityKey = firstBytes(root, {"identity_key"});
        keys.signedPrekey = firstBytes(root, {"signed_prekey"});
        keys.signedPrekeySignature = firstBytes(root, {"signed_prekey_signature"});
        keys.signedPrekeyId = static_cast<quint32>(
            root.value(QStringLiteral("signed_prekey_id")).toDouble(0));
        keys.oneTimePrekeyId = static_cast<quint32>(
            root.value(QStringLiteral("one_time_prekey_id")).toDouble(0));
        keys.oneTimePrekey = firstBytes(root, {"one_time_prekey"});
        if (keys.identityKey.size() != 32 || keys.signedPrekey.size() != 32
            || keys.signedPrekeySignature.size() != 64) {
            callback(ApiResult<PeerKeys>::fail(
                ApiError::protocol(QStringLiteral("peer_keys_invalid"))));
            return;
        }
        callback(ApiResult<PeerKeys>::ok(keys));
    });
}

void ApiClient::lookupUser(const QString &login, Callback<AccountProfile> callback)
{
    const QString path = QStringLiteral("/api/v1/users/")
        + QString::fromLatin1(QUrl::toPercentEncoding(login));
    send("GET", path, {}, false, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<AccountProfile>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<AccountProfile>::fail(error));
            return;
        }
        callback(ApiResult<AccountProfile>::ok(parseProfile(root)));
    });
}

} // namespace mini::net
