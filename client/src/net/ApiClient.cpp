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
#include <QUrl>

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

GroupMemberInfo parseGroupMember(const QJsonObject &object)
{
    GroupMemberInfo member;
    member.userId = firstString(object, {"user_id", "userId", "id"});
    member.login = firstString(object, {"login"});
    member.displayName = firstString(object, {"display_name", "displayName", "name"});
    member.role = firstString(object, {"role"});
    if (member.role.isEmpty()) {
        member.role = QStringLiteral("member");
    }
    member.permissions = static_cast<quint32>(
        object.value(QStringLiteral("permissions")).toDouble(0));
    member.joinedAt = toInt64(object.value(QStringLiteral("joined_at")));
    member.mutedUntil = toInt64(object.value(QStringLiteral("muted_until")));
    return member;
}

GroupInfo parseGroup(const QJsonObject &object)
{
    GroupInfo group;
    group.groupId = firstString(object, {"group_id", "id"});
    group.title = firstString(object, {"title"});
    group.about = firstString(object, {"about"});
    group.avatarMedia = firstString(object, {"avatar_media"});
    group.ownerId = firstString(object, {"owner_id"});
    group.isPublic = object.value(QStringLiteral("is_public")).toBool();
    group.slowModeSec = object.value(QStringLiteral("slow_mode_sec")).toInt();
    group.joinByRequest = object.value(QStringLiteral("join_by_request")).toBool();
    group.inviteToken = firstString(object, {"invite_token"});
    group.inviteLink = firstString(object, {"invite_link"});
    group.myRole = firstString(object, {"my_role"});
    group.myPermissions = static_cast<quint32>(
        object.value(QStringLiteral("my_permissions")).toDouble(0));
    group.memberCount = object.value(QStringLiteral("member_count")).toInt();
    group.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    group.updatedAt = toInt64(object.value(QStringLiteral("updated_at")));
    group.requestStatus = firstString(object, {"request_status"});
    const QJsonArray members = object.value(QStringLiteral("members")).toArray();
    for (const QJsonValue &value : members) {
        if (value.isObject()) {
            group.members.append(parseGroupMember(value.toObject()));
        }
    }
    if (group.memberCount == 0 && !group.members.isEmpty()) {
        group.memberCount = group.members.size();
    }
    return group;
}

ChannelMemberInfo parseChannelMember(const QJsonObject &object)
{
    ChannelMemberInfo member;
    member.userId = firstString(object, {"user_id", "userId", "id"});
    member.login = firstString(object, {"login"});
    member.displayName = firstString(object, {"display_name", "displayName", "name"});
    member.role = firstString(object, {"role"});
    if (member.role.isEmpty()) {
        member.role = QStringLiteral("subscriber");
    }
    member.permissions = static_cast<quint32>(
        object.value(QStringLiteral("permissions")).toDouble(0));
    member.joinedAt = toInt64(object.value(QStringLiteral("joined_at")));
    return member;
}

ChannelInfo parseChannel(const QJsonObject &object)
{
    ChannelInfo channel;
    channel.channelId = firstString(object, {"channel_id", "id"});
    channel.title = firstString(object, {"title"});
    channel.about = firstString(object, {"about"});
    channel.avatarMedia = firstString(object, {"avatar_media"});
    channel.ownerId = firstString(object, {"owner_id"});
    channel.isPublic = object.value(QStringLiteral("is_public")).toBool();
    channel.linkedGroup = firstString(object, {"linked_group"});
    channel.inviteToken = firstString(object, {"invite_token"});
    channel.inviteLink = firstString(object, {"invite_link"});
    channel.myRole = firstString(object, {"my_role"});
    channel.myPermissions = static_cast<quint32>(
        object.value(QStringLiteral("my_permissions")).toDouble(0));
    channel.subscriberCount = object.value(QStringLiteral("subscriber_count")).toInt();
    channel.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    channel.updatedAt = toInt64(object.value(QStringLiteral("updated_at")));
    const QJsonArray members = object.value(QStringLiteral("members")).toArray();
    for (const QJsonValue &value : members) {
        if (value.isObject()) {
            channel.members.append(parseChannelMember(value.toObject()));
        }
    }
    if (channel.subscriberCount == 0 && !channel.members.isEmpty()) {
        channel.subscriberCount = channel.members.size();
    }
    return channel;
}

GroupJoinRequestInfo parseGroupRequest(const QJsonObject &object)
{
    GroupJoinRequestInfo request;
    request.groupId = firstString(object, {"group_id"});
    request.userId = firstString(object, {"user_id"});
    request.login = firstString(object, {"login"});
    request.displayName = firstString(object, {"display_name"});
    request.message = firstString(object, {"message"});
    request.status = firstString(object, {"status"});
    if (request.status.isEmpty()) {
        request.status = QStringLiteral("pending");
    }
    request.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    return request;
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
    const QByteArray payload =
        method == "GET" ? QByteArray()
                        : QJsonDocument(body).toJson(QJsonDocument::Compact);
    perform(method, path, payload, QByteArrayLiteral("application/json"), authenticated,
            [this, callback](ApiResult<QByteArray> raw) {
                if (raw.isError()) {
                    callback(raw);
                    return;
                }
                const QByteArray &data = raw.value();
                if (!data.isEmpty()) {
                    const QJsonObject root = parseObjectOr(data);
                    const QJsonValue version =
                        root.value(QStringLiteral("protocol_version"));
                    if (!version.isUndefined() && version.toInt() != m_protocolVersion) {
                        emit protocolVersionMismatch(version.toInt());
                        callback(ApiResult<QByteArray>::fail(
                            ApiError::protocol(QStringLiteral("protocol_version_mismatch"))));
                        return;
                    }
                }
                callback(raw);
            });
}

void ApiClient::perform(const QByteArray &method, const QString &path,
                        const QByteArray &payload, const QByteArray &contentType,
                        bool authenticated, RawCallback callback)
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
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromLatin1(contentType.isEmpty() ? "application/json"
                                                                : contentType.constData()));
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

void ApiClient::listGroups(Callback<QList<GroupInfo>> callback)
{
    send("GET", QStringLiteral("/api/v1/groups"), {}, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<QList<GroupInfo>>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<QList<GroupInfo>>::fail(error));
                 return;
             }
             QList<GroupInfo> groups;
             const QJsonArray array = root.value(QStringLiteral("groups")).toArray();
             for (const QJsonValue &value : array) {
                 if (value.isObject()) {
                     groups.append(parseGroup(value.toObject()));
                 }
             }
             callback(ApiResult<QList<GroupInfo>>::ok(groups));
         });
}

void ApiClient::createGroup(const GroupCreateRequest &request, Callback<GroupInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("title"), request.title);
    body.insert(QStringLiteral("about"), request.about);
    if (!request.avatarMedia.isEmpty()) {
        body.insert(QStringLiteral("avatar_media"), request.avatarMedia);
    }
    body.insert(QStringLiteral("is_public"), request.isPublic);
    body.insert(QStringLiteral("join_by_request"), request.joinByRequest);
    if (request.slowModeSec > 0) {
        body.insert(QStringLiteral("slow_mode_sec"), request.slowModeSec);
    }
    QJsonArray members;
    for (const QString &member : request.members) {
        members.append(member);
    }
    body.insert(QStringLiteral("members"), members);

    send("POST", QStringLiteral("/api/v1/groups"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<GroupInfo>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<GroupInfo>::fail(error));
                 return;
             }
             const QJsonObject group = root.value(QStringLiteral("group")).toObject();
             callback(ApiResult<GroupInfo>::ok(parseGroup(group)));
         });
}

void ApiClient::getGroup(const QString &groupId, Callback<GroupInfo> callback)
{
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId));
    send("GET", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupInfo>::ok(
            parseGroup(root.value(QStringLiteral("group")).toObject())));
    });
}

void ApiClient::updateGroup(const QString &groupId, const GroupPatch &patch,
                            Callback<GroupInfo> callback)
{
    QJsonObject body;
    if (patch.title.has_value()) {
        body.insert(QStringLiteral("title"), *patch.title);
    }
    if (patch.about.has_value()) {
        body.insert(QStringLiteral("about"), *patch.about);
    }
    if (patch.avatarMedia.has_value()) {
        body.insert(QStringLiteral("avatar_media"), *patch.avatarMedia);
    }
    if (patch.isPublic.has_value()) {
        body.insert(QStringLiteral("is_public"), *patch.isPublic);
    }
    if (patch.joinByRequest.has_value()) {
        body.insert(QStringLiteral("join_by_request"), *patch.joinByRequest);
    }
    if (patch.slowModeSec.has_value()) {
        body.insert(QStringLiteral("slow_mode_sec"), *patch.slowModeSec);
    }

    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId));
    send("PATCH", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupInfo>::ok(
            parseGroup(root.value(QStringLiteral("group")).toObject())));
    });
}

void ApiClient::addGroupMember(const QString &groupId, const QString &userIdOrLogin,
                               Callback<GroupMemberInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userIdOrLogin);
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId))
        + QStringLiteral("/members");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupMemberInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupMemberInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupMemberInfo>::ok(
            parseGroupMember(root.value(QStringLiteral("member")).toObject())));
    });
}

void ApiClient::removeGroupMember(const QString &groupId, const QString &userId, bool ban,
                                  int durationHours, Callback<Empty> callback)
{
    QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/members/")
        + QString::fromLatin1(QUrl::toPercentEncoding(userId));
    if (ban) {
        path += QStringLiteral("?ban=true");
        if (durationHours > 0) {
            path += QStringLiteral("&duration_hours=") + QString::number(durationHours);
        }
    }
    send("DELETE", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<Empty>::fail(raw.error()));
            return;
        }
        callback(ApiResult<Empty>::ok(Empty()));
    });
}

void ApiClient::setGroupRole(const QString &groupId, const QString &userId,
                             const QString &role, quint32 permissions,
                             Callback<GroupMemberInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    body.insert(QStringLiteral("role"), role);
    body.insert(QStringLiteral("permissions"), static_cast<qint64>(permissions));
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/role");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupMemberInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupMemberInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupMemberInfo>::ok(
            parseGroupMember(root.value(QStringLiteral("member")).toObject())));
    });
}

void ApiClient::transferGroupOwnership(const QString &groupId, const QString &userId,
                                       Callback<GroupInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/transfer");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupInfo>::ok(
            parseGroup(root.value(QStringLiteral("group")).toObject())));
    });
}

void ApiClient::createGroupInvite(const QString &groupId, Callback<GroupInvite> callback)
{
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/invite");
    send("POST", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupInvite>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupInvite>::fail(error));
            return;
        }
        GroupInvite invite;
        invite.groupId = firstString(root, {"group_id"});
        invite.token = firstString(root, {"invite_token", "token"});
        invite.link = firstString(root, {"invite_link", "link"});
        callback(ApiResult<GroupInvite>::ok(invite));
    });
}

void ApiClient::joinGroup(const QString &token, const QString &message,
                          Callback<GroupJoinResult> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("token"), token);
    if (!message.isEmpty()) {
        body.insert(QStringLiteral("message"), message);
    }
    send("POST", QStringLiteral("/api/v1/groups/join"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<GroupJoinResult>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<GroupJoinResult>::fail(error));
                 return;
             }
             GroupJoinResult result;
             result.status = firstString(root, {"status"});
             result.group = parseGroup(root.value(QStringLiteral("group")).toObject());
             if (result.group.groupId.isEmpty()) {
                 result.group.groupId = firstString(root, {"group_id"});
             }
             callback(ApiResult<GroupJoinResult>::ok(result));
         });
}

void ApiClient::requestGroupJoin(const QString &groupId, const QString &message,
                                 Callback<GroupJoinRequestInfo> callback)
{
    QJsonObject body;
    if (!message.isEmpty()) {
        body.insert(QStringLiteral("message"), message);
    }
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/request");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupJoinRequestInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupJoinRequestInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupJoinRequestInfo>::ok(
            parseGroupRequest(root.value(QStringLiteral("request")).toObject())));
    });
}

void ApiClient::listGroupRequests(const QString &groupId, const QString &status,
                                  Callback<QList<GroupJoinRequestInfo>> callback)
{
    QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/requests");
    if (!status.isEmpty()) {
        path += QStringLiteral("?status=") + QString::fromLatin1(QUrl::toPercentEncoding(status));
    }
    send("GET", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<QList<GroupJoinRequestInfo>>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<QList<GroupJoinRequestInfo>>::fail(error));
            return;
        }
        QList<GroupJoinRequestInfo> requests;
        const QJsonArray array = root.value(QStringLiteral("requests")).toArray();
        for (const QJsonValue &value : array) {
            if (value.isObject()) {
                requests.append(parseGroupRequest(value.toObject()));
            }
        }
        callback(ApiResult<QList<GroupJoinRequestInfo>>::ok(requests));
    });
}

void ApiClient::approveGroupRequest(const QString &groupId, const QString &userId,
                                    bool approve, Callback<GroupJoinRequestInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    body.insert(QStringLiteral("action"),
                approve ? QStringLiteral("approve") : QStringLiteral("reject"));
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/approve");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<GroupJoinRequestInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<GroupJoinRequestInfo>::fail(error));
            return;
        }
        callback(ApiResult<GroupJoinRequestInfo>::ok(
            parseGroupRequest(root.value(QStringLiteral("request")).toObject())));
    });
}

void ApiClient::banGroupMember(const QString &groupId, const QString &userId, bool permanent,
                               int durationHours, const QString &reason,
                               Callback<Empty> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    body.insert(QStringLiteral("permanent"), permanent);
    if (!permanent && durationHours > 0) {
        body.insert(QStringLiteral("duration_hours"), durationHours);
    }
    if (!reason.isEmpty()) {
        body.insert(QStringLiteral("reason"), reason);
    }
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/ban");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<Empty>::fail(raw.error()));
            return;
        }
        callback(ApiResult<Empty>::ok(Empty()));
    });
}

void ApiClient::unbanGroupMember(const QString &groupId, const QString &userId,
                                 Callback<Empty> callback)
{
    const QString path = QStringLiteral("/api/v1/groups/")
        + QString::fromLatin1(QUrl::toPercentEncoding(groupId)) + QStringLiteral("/ban/")
        + QString::fromLatin1(QUrl::toPercentEncoding(userId));
    send("DELETE", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<Empty>::fail(raw.error()));
            return;
        }
        callback(ApiResult<Empty>::ok(Empty()));
    });
}

void ApiClient::listChannels(Callback<QList<ChannelInfo>> callback)
{
    send("GET", QStringLiteral("/api/v1/channels"), {}, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<QList<ChannelInfo>>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<QList<ChannelInfo>>::fail(error));
                 return;
             }
             QList<ChannelInfo> channels;
             const QJsonArray array = root.value(QStringLiteral("channels")).toArray();
             for (const QJsonValue &value : array) {
                 if (value.isObject()) {
                     channels.append(parseChannel(value.toObject()));
                 }
             }
             callback(ApiResult<QList<ChannelInfo>>::ok(channels));
         });
}

void ApiClient::createChannel(const ChannelCreateRequest &request,
                              Callback<ChannelInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("title"), request.title);
    body.insert(QStringLiteral("about"), request.about);
    if (!request.avatarMedia.isEmpty()) {
        body.insert(QStringLiteral("avatar_media"), request.avatarMedia);
    }
    body.insert(QStringLiteral("is_public"), request.isPublic);
    if (!request.linkedGroup.isEmpty()) {
        body.insert(QStringLiteral("linked_group"), request.linkedGroup);
    }
    send("POST", QStringLiteral("/api/v1/channels"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<ChannelInfo>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<ChannelInfo>::fail(error));
                 return;
             }
             callback(ApiResult<ChannelInfo>::ok(
                 parseChannel(root.value(QStringLiteral("channel")).toObject())));
         });
}

void ApiClient::getChannel(const QString &channelId, Callback<ChannelInfo> callback)
{
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId));
    send("GET", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<ChannelInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<ChannelInfo>::fail(error));
            return;
        }
        callback(ApiResult<ChannelInfo>::ok(
            parseChannel(root.value(QStringLiteral("channel")).toObject())));
    });
}

void ApiClient::updateChannel(const QString &channelId, const ChannelPatch &patch,
                              Callback<ChannelInfo> callback)
{
    QJsonObject body;
    if (patch.title.has_value()) {
        body.insert(QStringLiteral("title"), *patch.title);
    }
    if (patch.about.has_value()) {
        body.insert(QStringLiteral("about"), *patch.about);
    }
    if (patch.avatarMedia.has_value()) {
        body.insert(QStringLiteral("avatar_media"), *patch.avatarMedia);
    }
    if (patch.isPublic.has_value()) {
        body.insert(QStringLiteral("is_public"), *patch.isPublic);
    }
    if (patch.linkedGroup.has_value()) {
        body.insert(QStringLiteral("linked_group"), *patch.linkedGroup);
    }
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId));
    send("PATCH", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<ChannelInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<ChannelInfo>::fail(error));
            return;
        }
        callback(ApiResult<ChannelInfo>::ok(
            parseChannel(root.value(QStringLiteral("channel")).toObject())));
    });
}

void ApiClient::deleteChannel(const QString &channelId, Callback<Empty> callback)
{
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId));
    send("DELETE", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<Empty>::fail(raw.error()));
            return;
        }
        callback(ApiResult<Empty>::ok(Empty()));
    });
}

void ApiClient::subscribeChannel(const QString &channelId, const QString &token,
                                 Callback<QString> callback)
{
    QJsonObject body;
    if (!token.isEmpty()) {
        body.insert(QStringLiteral("token"), token);
    }
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/subscribe");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<QString>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<QString>::fail(error));
            return;
        }
        callback(ApiResult<QString>::ok(firstString(root, {"status"})));
    });
}

void ApiClient::unsubscribeChannel(const QString &channelId, Callback<QString> callback)
{
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/unsubscribe");
    send("POST", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<QString>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<QString>::fail(error));
            return;
        }
        callback(ApiResult<QString>::ok(firstString(root, {"status"})));
    });
}

void ApiClient::listChannelMembers(const QString &channelId,
                                   Callback<QList<ChannelMemberInfo>> callback)
{
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/members");
    send("GET", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<QList<ChannelMemberInfo>>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<QList<ChannelMemberInfo>>::fail(error));
            return;
        }
        QList<ChannelMemberInfo> members;
        const QJsonArray array = root.value(QStringLiteral("members")).toArray();
        for (const QJsonValue &value : array) {
            if (value.isObject()) {
                members.append(parseChannelMember(value.toObject()));
            }
        }
        callback(ApiResult<QList<ChannelMemberInfo>>::ok(members));
    });
}

void ApiClient::setChannelRole(const QString &channelId, const QString &userId,
                               const QString &role, quint32 permissions,
                               Callback<ChannelMemberInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    body.insert(QStringLiteral("role"), role);
    body.insert(QStringLiteral("permissions"), static_cast<double>(permissions));
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/role");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<ChannelMemberInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<ChannelMemberInfo>::fail(error));
            return;
        }
        callback(ApiResult<ChannelMemberInfo>::ok(
            parseChannelMember(root.value(QStringLiteral("member")).toObject())));
    });
}

void ApiClient::createChannelInvite(const QString &channelId, Callback<ChannelInvite> callback)
{
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/invite");
    send("POST", path, {}, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<ChannelInvite>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<ChannelInvite>::fail(error));
            return;
        }
        ChannelInvite invite;
        invite.channelId = firstString(root, {"channel_id"});
        invite.token = firstString(root, {"invite_token"});
        invite.link = firstString(root, {"invite_link"});
        callback(ApiResult<ChannelInvite>::ok(invite));
    });
}

void ApiClient::recordChannelView(const QString &channelId, const QString &postId,
                                  Callback<ChannelViewInfo> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("post_id"), postId);
    const QString path = QStringLiteral("/api/v1/channels/")
        + QString::fromLatin1(QUrl::toPercentEncoding(channelId))
        + QStringLiteral("/view");
    send("POST", path, body, true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<ChannelViewInfo>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<ChannelViewInfo>::fail(error));
            return;
        }
        ChannelViewInfo view;
        view.status = firstString(root, {"status"});
        view.channelId = firstString(root, {"channel_id"});
        view.postId = firstString(root, {"post_id"});
        view.recorded = root.value(QStringLiteral("recorded")).toBool();
        view.views = root.contains(QStringLiteral("views"))
            ? root.value(QStringLiteral("views")).toInt()
            : -1;
        callback(ApiResult<ChannelViewInfo>::ok(view));
    });
}

void ApiClient::mediaInit(qint64 totalSize, qint64 chunkSize,
                          Callback<MediaUploadSession> callback)
{
    QJsonObject body;
    body.insert(QStringLiteral("total_size"), static_cast<double>(totalSize));
    body.insert(QStringLiteral("chunk_size"), static_cast<double>(chunkSize));
    send("POST", QStringLiteral("/api/v1/media/init"), body, true,
         [callback](ApiResult<QByteArray> raw) {
             if (raw.isError()) {
                 callback(ApiResult<MediaUploadSession>::fail(raw.error()));
                 return;
             }
             QJsonObject root;
             ApiError error;
             if (!parseObject(raw.value(), &root, &error)) {
                 callback(ApiResult<MediaUploadSession>::fail(error));
                 return;
             }
             MediaUploadSession session;
             session.uploadId = firstString(root, {"upload_id"});
             session.totalSize =
                 static_cast<qint64>(root.value(QStringLiteral("total_size")).toDouble());
             session.chunkSize =
                 static_cast<qint64>(root.value(QStringLiteral("chunk_size")).toDouble());
             session.chunkCount = root.value(QStringLiteral("chunk_count")).toInt();
             session.expiresAt =
                 static_cast<qint64>(root.value(QStringLiteral("expires_at")).toDouble());
             if (session.uploadId.isEmpty() || session.chunkCount <= 0) {
                 callback(ApiResult<MediaUploadSession>::fail(
                     ApiError::protocol(QStringLiteral("media_init_response"))));
                 return;
             }
             callback(ApiResult<MediaUploadSession>::ok(session));
         });
}

void ApiClient::mediaPutChunk(const QString &uploadId, int idx, const QByteArray &data,
                              Callback<MediaChunkResult> callback)
{
    const QString path = QStringLiteral("/api/v1/media/chunk/")
        + QString::fromLatin1(QUrl::toPercentEncoding(uploadId)) + QLatin1Char('/')
        + QString::number(idx);
    perform("PUT", path, data, QByteArrayLiteral("application/octet-stream"), true,
            [callback](ApiResult<QByteArray> raw) {
                if (raw.isError()) {
                    callback(ApiResult<MediaChunkResult>::fail(raw.error()));
                    return;
                }
                QJsonObject root;
                ApiError error;
                if (!parseObject(raw.value(), &root, &error)) {
                    callback(ApiResult<MediaChunkResult>::fail(error));
                    return;
                }
                MediaChunkResult chunk;
                chunk.uploadId = firstString(root, {"upload_id"});
                chunk.idx = root.value(QStringLiteral("idx")).toInt(-1);
                chunk.received =
                    static_cast<qint64>(root.value(QStringLiteral("received")).toDouble());
                chunk.receivedChunks =
                    root.value(QStringLiteral("received_chunks")).toInt();
                chunk.chunkCount = root.value(QStringLiteral("chunk_count")).toInt();
                callback(ApiResult<MediaChunkResult>::ok(chunk));
            });
}

void ApiClient::mediaComplete(const QString &uploadId,
                              Callback<MediaCompleteResult> callback)
{
    const QString path = QStringLiteral("/api/v1/media/complete/")
        + QString::fromLatin1(QUrl::toPercentEncoding(uploadId));
    send("POST", path, QJsonObject(), true, [callback](ApiResult<QByteArray> raw) {
        if (raw.isError()) {
            callback(ApiResult<MediaCompleteResult>::fail(raw.error()));
            return;
        }
        QJsonObject root;
        ApiError error;
        if (!parseObject(raw.value(), &root, &error)) {
            callback(ApiResult<MediaCompleteResult>::fail(error));
            return;
        }
        MediaCompleteResult result;
        result.uploadId = firstString(root, {"upload_id"});
        result.totalSize =
            static_cast<qint64>(root.value(QStringLiteral("total_size")).toDouble());
        result.chunkCount = root.value(QStringLiteral("chunk_count")).toInt();
        result.completedAt =
            static_cast<qint64>(root.value(QStringLiteral("completed_at")).toDouble());
        callback(ApiResult<MediaCompleteResult>::ok(result));
    });
}

void ApiClient::mediaGetChunk(const QString &uploadId, int idx,
                              Callback<QByteArray> callback)
{
    const QString path = QStringLiteral("/api/v1/media/")
        + QString::fromLatin1(QUrl::toPercentEncoding(uploadId)) + QLatin1Char('/')
        + QString::number(idx);
    perform("GET", path, {}, {}, true,
            [callback](ApiResult<QByteArray> raw) { callback(raw); });
}

} // namespace mini::net
