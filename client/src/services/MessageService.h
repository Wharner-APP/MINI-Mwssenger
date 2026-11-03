#pragma once

#include "core/Result.h"
#include "domain/Message.h"
#include "domain/OutboxItem.h"
#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::net {
class ApiClient;
class WsClient;
} // namespace mini::net

Q_DECLARE_METATYPE(mini::Message)

namespace mini::services {

// Encrypts and decrypts message bodies for one chat. The real Double Ratchet
// session store is wired in T08; tests use a deterministic stub.
class CryptoSession
{
public:
    virtual ~CryptoSession() = default;

    virtual Result<QByteArray> encrypt(const QString &chatId, const QByteArray &plaintext,
                                       QJsonObject *header) = 0;
    virtual Result<QByteArray> decrypt(const QString &chatId, const QByteArray &ciphertext,
                                       const QJsonObject &header) = 0;

    // True when encrypt() can run for this chat without a network round-trip.
    // Plain stub sessions are always ready; the real one resolves the peer's
    // public prekey first.
    [[nodiscard]] virtual bool isReady(const QString &chatId) const
    {
        Q_UNUSED(chatId);
        return true;
    }
    virtual void prepare(const QString &chatId, const QString &peerLogin,
                         std::function<void(Result<bool>)> done)
    {
        Q_UNUSED(chatId);
        Q_UNUSED(peerLogin);
        done(Result<bool>::ok(true));
    }
};

// Network seam of the send pipeline. Production uses ApiMessageTransport;
// tests implement it locally without a server.
class MessageTransport : public QObject
{
    Q_OBJECT

public:
    using SendCallback = std::function<void(const net::ApiResult<net::SendResult> &)>;

    explicit MessageTransport(QObject *parent = nullptr);
    ~MessageTransport() override;

    virtual void send(const QList<net::Envelope> &envelopes, SendCallback done) = 0;
    [[nodiscard]] virtual bool isOnline() const { return true; }

signals:
    void messageReceived(const mini::net::Envelope &envelope);
    void acknowledged(const QString &clientMsgId, const QString &chatId);
    void delivered(const QString &clientMsgId, const QString &chatId, qint64 at);
    void readReceipt(const QString &chatId, const QStringList &clientMsgIds);
    void onlineChanged(bool online);
};

// Adapts ApiClient + WsClient to MessageTransport. Server mail/WS land in T09,
// so no endpoint is assumed here beyond the existing ApiClient contract.
class ApiMessageTransport : public MessageTransport
{
    Q_OBJECT

public:
    ApiMessageTransport(net::ApiClient *api, net::WsClient *ws, QObject *parent = nullptr);

    void send(const QList<net::Envelope> &envelopes, SendCallback done) override;
    [[nodiscard]] bool isOnline() const override;

    // Pulls queued mail on every WS connect and on a timer, then acks what the
    // service has stored. This is what makes offline delivery work.
    void setDeviceId(const QString &deviceId);
    void setPullInterval(int milliseconds);
    void pullNow();

private:
    void pullBatch(const QString &cursor, const QStringList &mailIds);
    void finishPull(const QStringList &mailIds);

    net::ApiClient *m_api = nullptr;
    net::WsClient *m_ws = nullptr;
    QTimer *m_pullTimer = nullptr;
    QString m_deviceId;
    bool m_pulling = false;
};

// The only owner of the outgoing pipeline: DB row first, then outbox, then
// the network. UI gets a client_msg_id immediately and then listens to
// messageAdded / messageUpdated.
class MessageService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit MessageService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setCryptoSession(CryptoSession *session) { m_crypto = session; }
    void setTransport(MessageTransport *transport);
    [[nodiscard]] MessageTransport *transport() const { return m_transport; }
    // Stamped into every envelope header so the server can route the envelope
    // and the recipient can label the chat.
    void setSelfLogin(const QString &login) { m_selfLogin = login; }

    // Test/UI seam: the window during which a queued message can be cancelled.
    void setCancelWindowMs(int milliseconds);
    void setRetryDelays(const QVector<int> &milliseconds);

    // Returns the id even before the database write completes; an empty id
    // means the request was rejected (empty text or chat).
    QString sendText(const QString &chatId, const QString &text,
                     const QString &replyToClientMsgId = QString(),
                     const QString &clientMsgId = QString());

    // True only while the message has not left the device and is inside the
    // cancel window; never reports success for a message that was sent.
    void cancelSend(const QString &clientMsgId, Done<bool> done);
    void deleteLocal(const QString &clientMsgId, Done<bool> done);
    void loadHistory(const QString &chatId, const MessageCursor &before, int limit,
                     Done<QVector<Message>> done);
    void markChatRead(const QString &chatId, Done<int> done);

    // Retries held/delayed outbox heads (reconnect, F5, the retry button).
    void flushQueue();
    void retryQueue() { flushQueue(); }

signals:
    void messageAdded(const mini::Message &message);
    void messageUpdated(const mini::Message &message);
    void messageRemoved(const QString &clientMsgId, const QString &chatId);
    // A failed send returns its text so the composer can restore it.
    void messageSendFailed(const QString &clientMsgId, const QString &chatId,
                           const QString &text, const QString &code);
    void pendingChanged(int pending);
    void errorOccurred(const QString &code, const QString &detail);

private:
    void schedulePump(int delayMs);
    void pump();
    void sendHead(const OutboxItem &item);
    void markFailed(const OutboxItem &item, const QString &code);
    void handleSendFailure(const OutboxItem &item, const net::ApiError &error);
    void handleIncoming(const net::Envelope &envelope);
    void handleAcknowledged(const QString &clientMsgId, const QString &chatId);
    void handleDelivered(const QString &clientMsgId, const QString &chatId, qint64 at);
    void handleReadReceipt(const QString &chatId, const QStringList &clientMsgIds);

    storage::StorageWorker *m_storage = nullptr;
    CryptoSession *m_crypto = nullptr;
    MessageTransport *m_transport = nullptr;
    QVector<QMetaObject::Connection> m_transportConnections;
    QTimer m_pumpTimer;
    QHash<QString, qint64> m_holdUntil;
    QSet<QString> m_sendingChats;
    int m_cancelWindowMs = 5000;
    QVector<int> m_retryDelays{1000, 2000, 4000};
    QString m_selfLogin;
};

} // namespace mini::services
