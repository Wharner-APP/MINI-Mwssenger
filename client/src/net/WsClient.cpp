#include "net/WsClient.h"

#include "net/ApiClient.h"
#include "net/TlsPin.h"
#include "net/WebSocketConnection.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>

namespace mini::net {

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString stringValue(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

Envelope parseEnvelope(const QJsonObject &object)
{
    Envelope envelope;
    envelope.kind = stringValue(object, "kind");
    envelope.clientMsgId = stringValue(object, "client_msg_id");
    envelope.chatId = stringValue(object, "chat_id");
    envelope.ciphertext =
        QByteArray::fromBase64(stringValue(object, "ciphertext_b64").toLatin1());
    envelope.header = object.value(QStringLiteral("header")).toObject();
    return envelope;
}

} // namespace

WsClient::WsClient(ApiClient *api, TlsPin *pin, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_pin(pin)
    , m_connection(new WebSocketConnection(pin, this))
    , m_reconnectTimer(new QTimer(this))
    , m_pingTimer(new QTimer(this))
    , m_pongTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    m_pongTimer->setSingleShot(true);
    m_pingTimer->setInterval(m_pingIntervalMs);

    connect(m_reconnectTimer, &QTimer::timeout, this, &WsClient::connectOnce);
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        // RFC 6455 control ping; a JSON "pong" is accepted as well.
        m_connection->sendPing(QByteArray::number(nowMs()));
        m_pongTimer->start(m_pongTimeoutMs);
    });
    connect(m_pongTimer, &QTimer::timeout, this, [this]() {
        m_connection->close(4000, QStringLiteral("pong_timeout"));
    });

    connect(m_connection, &WebSocketConnection::opened, this, &WsClient::handleOpen);
    connect(m_connection, &WebSocketConnection::closed, this, &WsClient::handleClosed);
    connect(m_connection, &WebSocketConnection::textMessageReceived,
            this, &WsClient::handleText);
    connect(m_connection, &WebSocketConnection::pongReceived,
            this, [this]() {
                m_pongTimer->stop();
                emit pongReceived();
            });
    connect(m_connection, &WebSocketConnection::transportError, this,
            [this](const QString &reason, bool tlsMismatch) {
                if (tlsMismatch) {
                    // TlsPin also emits mismatchDetected/pinUnavailable; this
                    // signal covers the no-pin-object case and is the UI hook.
                    emit tlsWarning(reason);
                }
            });
}

WsClient::~WsClient()
{
    m_stopped = true;
    stopTimers();
    if (m_connection) {
        m_connection->disconnect(this);
        m_connection->close();
    }
}

void WsClient::start(const Endpoint &endpoint, const QString &accessToken)
{
    if (!m_api || !endpoint.isValid()) {
        return;
    }
    m_endpoint = endpoint;
    m_accessToken = accessToken;
    m_stopped = false;
    m_attempts = 0;
    m_policy.reset();
    m_reconnectTimer->stop();
    connectOnce();
}

void WsClient::subscribePresence(const QStringList &logins)
{
    if (!isConnected() || logins.isEmpty()) {
        return;
    }
    QStringList cleaned;
    cleaned.reserve(qMin(logins.size(), 200));
    for (const QString &login : logins) {
        const QString trimmed = login.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed)) {
            cleaned.append(trimmed);
        }
        if (cleaned.size() >= 200) {
            break;
        }
    }
    if (cleaned.isEmpty()) {
        return;
    }
    QJsonObject data;
    QJsonArray array;
    for (const QString &login : cleaned) {
        array.append(login);
    }
    data.insert(QStringLiteral("logins"), array);
    sendFrame(QStringLiteral("subscribe_presence"), data);
}

void WsClient::reconnect()
{
    if (!m_endpoint.isValid() || m_accessToken.isEmpty()) {
        return;
    }
    m_stopped = false;
    m_attempts = 0;
    m_policy.reset();
    m_reconnectTimer->stop();
    if (isConnected()) {
        return;
    }
    connectOnce();
}

void WsClient::stop()
{
    m_stopped = true;
    m_reconnectTimer->stop();
    stopTimers();
    if (m_connection->state() != WebSocketConnection::State::Closed
        && m_connection->state() != WebSocketConnection::State::Idle) {
        m_connection->close(1000, QStringLiteral("client_stop"));
    }
}

bool WsClient::isConnected() const
{
    return m_connection && m_connection->state() == WebSocketConnection::State::Open;
}

void WsClient::setPingInterval(int milliseconds)
{
    m_pingIntervalMs = milliseconds;
    m_pingTimer->setInterval(milliseconds);
}

void WsClient::setPongTimeout(int milliseconds)
{
    m_pongTimeoutMs = milliseconds;
}

void WsClient::connectOnce()
{
    if (m_stopped || !m_api) {
        return;
    }

    // The ticket request goes through ApiClient's endpoint; the socket uses
    // the endpoint passed to start(). In production they are the same server.
    m_api->setAccessToken(m_accessToken);

    QPointer<WsClient> guard(this);
    m_api->requestWsTicket([this, guard](ApiResult<WsTicket> result) {
        if (guard.isNull() || m_stopped) {
            return;
        }
        if (result.isError()) {
            if (result.error().kind == ApiErrorKind::Unauthorized) {
                m_stopped = true;
                emit authenticationFailed();
                return;
            }
            scheduleReconnect();
            return;
        }

        QUrl url = m_endpoint.wsUrl(QStringLiteral("/ws"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("ticket"), result.value().ticket);
        url.setQuery(query);
        m_connection->open(url);
    });
}

void WsClient::scheduleReconnect()
{
    if (m_stopped) {
        return;
    }
    if (m_attempts >= ReconnectPolicy::kMaxAttempts) {
        m_stopped = true;
        emit reconnectExhausted();
        return;
    }

    const qint64 delay = m_policy.nextDelay();
    ++m_attempts;
    emit reconnectScheduled(static_cast<int>(delay), m_attempts);
    m_reconnectTimer->start(static_cast<int>(delay));
}

void WsClient::handleOpen()
{
    m_attempts = 0;
    m_policy.reset();
    m_pingTimer->start();

    QJsonObject hello;
    hello.insert(QStringLiteral("protocol_version"),
                 m_api ? m_api->protocolVersion() : 1);
    sendFrame(QStringLiteral("hello"), hello);
    emit connected();
}

void WsClient::handleClosed(int code, const QString &reason)
{
    stopTimers();

    if (code == 4401 || (code == 1008 && reason.contains(QStringLiteral("unauthorized"),
                                                         Qt::CaseInsensitive))) {
        m_stopped = true;
        emit disconnected(code, reason);
        emit authenticationFailed();
        return;
    }

    emit disconnected(code, reason);
    if (!m_stopped) {
        scheduleReconnect();
    }
}

void WsClient::handleText(const QString &text)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QStringLiteral("protocol_error"), QStringLiteral("invalid_frame"));
        return;
    }

    const QJsonObject frame = document.object();
    if (frame.value(QStringLiteral("v")).toInt(1) != 1) {
        emit serverError(QStringLiteral("protocol_version"),
                         QStringLiteral("unsupported_protocol_version"));
        m_connection->close(4001, QStringLiteral("protocol_version"));
        return;
    }

    const QString type = frame.value(QStringLiteral("type")).toString();
    const QJsonObject data = frame.value(QStringLiteral("data")).toObject();
    const qint64 ts = static_cast<qint64>(frame.value(QStringLiteral("ts")).toDouble());

    if (type == QStringLiteral("hello")) {
        const QJsonValue version = data.value(QStringLiteral("protocol_version"));
        const QString serverVersion = version.isString() ? version.toString()
                                                         : QString::number(version.toInt());
        emit helloReceived(serverVersion,
                           static_cast<qint64>(data.value(QStringLiteral("server_time"))
                                                   .toDouble(static_cast<double>(ts))));
    } else if (type == QStringLiteral("message")) {
        QJsonObject envelopeObject = data.value(QStringLiteral("envelope")).toObject();
        if (envelopeObject.isEmpty()) {
            envelopeObject = data;
        }
        emit messageReceived(parseEnvelope(envelopeObject));
    } else if (type == QStringLiteral("ack")) {
        emit ackReceived(stringValue(data, "client_msg_id"), stringValue(data, "chat_id"));
    } else if (type == QStringLiteral("delivery")) {
        emit delivery(stringValue(data, "client_msg_id"), stringValue(data, "chat_id"),
                      static_cast<qint64>(data.value(QStringLiteral("at")).toDouble(static_cast<double>(ts))));
    } else if (type == QStringLiteral("read")) {
        QStringList ids;
        const QJsonArray array = data.value(QStringLiteral("client_msg_ids")).toArray();
        for (const QJsonValue &value : array) {
            if (value.isString()) {
                ids.append(value.toString());
            }
        }
        if (ids.isEmpty()) {
            const QString single = stringValue(data, "client_msg_id");
            if (!single.isEmpty()) {
                ids.append(single);
            }
        }
        emit read(stringValue(data, "chat_id"), ids);
    } else if (type == QStringLiteral("presence")) {
        emit presence(stringValue(data, "user_id"),
                      data.value(QStringLiteral("online")).toBool(),
                      static_cast<qint64>(data.value(QStringLiteral("last_active_at")).toDouble()));
    } else if (type == QStringLiteral("typing")) {
        emit typing(stringValue(data, "chat_id"), stringValue(data, "user_id"),
                    data.value(QStringLiteral("active")).toBool());
    } else if (type == QStringLiteral("error")) {
        emit serverError(stringValue(data, "code"), stringValue(data, "message"));
    } else if (type == QStringLiteral("pong")) {
        m_pongTimer->stop();
        emit pongReceived();
    } else if (type == QStringLiteral("ping")) {
        sendFrame(QStringLiteral("pong"), data);
    }
    // Unknown types are ignored: protocol_version 1 allows optional frames.
}

void WsClient::sendFrame(const QString &type, const QJsonObject &data)
{
    QJsonObject frame;
    frame.insert(QStringLiteral("v"), 1);
    frame.insert(QStringLiteral("type"), type);
    frame.insert(QStringLiteral("ts"), static_cast<double>(nowMs()));
    frame.insert(QStringLiteral("data"), data);
    m_connection->sendText(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void WsClient::stopTimers()
{
    m_pingTimer->stop();
    m_pongTimer->stop();
}

} // namespace mini::net
