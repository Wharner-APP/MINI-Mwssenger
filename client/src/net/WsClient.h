#pragma once

#include "net/ApiTypes.h"
#include "net/Endpoint.h"
#include "net/ReconnectPolicy.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace mini::net {

class ApiClient;
class TlsPin;
class WebSocketConnection;

// WebSocket session: fetches a short-lived ticket, opens the socket, decodes
// frames and reconnects with exponential backoff. A 401 stops reconnecting.
class WsClient : public QObject
{
    Q_OBJECT

public:
    explicit WsClient(ApiClient *api, TlsPin *pin = nullptr, QObject *parent = nullptr);
    ~WsClient() override;

    void start(const Endpoint &endpoint, const QString &accessToken);
    void stop();

    // Asks the server for presence snapshots/updates of the given logins.
    // Frames are optional in v1: a server that does not know the type ignores
    // them, and the UI simply keeps the last known state.
    void subscribePresence(const QStringList &logins);
    // F5: forget backoff and try again now.
    void reconnect();

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] int reconnectAttempts() const { return m_attempts; }

    // Test hooks; production uses 20 s ping and 10 s pong timeout.
    void setPingInterval(int milliseconds);
    void setPongTimeout(int milliseconds);

signals:
    void connected();
    void helloReceived(const QString &serverVersion, qint64 serverTime);
    void messageReceived(const mini::net::Envelope &envelope);
    void ackReceived(const QString &clientMsgId, const QString &chatId);
    void delivery(const QString &clientMsgId, const QString &chatId, qint64 at);
    void read(const QString &chatId, const QStringList &clientMsgIds);
    void presence(const QString &userId, bool online, qint64 lastActiveAt);
    void typing(const QString &chatId, const QString &userId, bool active);
    void serverError(const QString &code, const QString &message);
    void tlsWarning(const QString &reason);
    void pongReceived();
    void disconnected(int code, const QString &reason);
    void authenticationFailed();
    void reconnectScheduled(int delayMs, int attempt);
    void reconnectExhausted();

private:
    void connectOnce();
    void scheduleReconnect();
    void handleOpen();
    void handleClosed(int code, const QString &reason);
    void handleText(const QString &text);
    void sendFrame(const QString &type, const QJsonObject &data = QJsonObject());
    void startPing();
    void stopTimers();

    ApiClient *m_api = nullptr;
    TlsPin *m_pin = nullptr;
    WebSocketConnection *m_connection = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_pongTimer = nullptr;
    Endpoint m_endpoint;
    QString m_accessToken;
    ReconnectPolicy m_policy;
    int m_attempts = 0;
    int m_pingIntervalMs = 20000;
    int m_pongTimeoutMs = 10000;
    bool m_stopped = true;
};

} // namespace mini::net
