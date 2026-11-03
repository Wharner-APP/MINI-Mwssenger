#pragma once

#include "core/Result.h"
#include "domain/Message.h"
#include "domain/OutboxItem.h"
#include "net/ApiError.h"
#include "net/ApiTypes.h"
#include "services/GroupCrypto.h"

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
    void setGroupCrypto(GroupCryptoSession *session) { m_groupCrypto = session; }
    [[nodiscard]] GroupCryptoSession *groupCrypto() const { return m_groupCrypto; }
    // Channels reuse the sender-key pipeline with the channel id as the
    // container id (§29.9). Kept separate so group state never sees channels.
    void setChannelCrypto(GroupCryptoSession *session) { m_channelCrypto = session; }
    [[nodiscard]] GroupCryptoSession *channelCrypto() const { return m_channelCrypto; }
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
    // Same pipeline for an already uploaded file (§18.4 step 7). `caption`
    // becomes the message body; the media descriptor travels inside the E2E
    // envelope, never in the clear.
    QString sendMedia(const QString &chatId, const QString &mediaId,
                      const QString &caption,
                      const QString &replyToClientMsgId = QString(),
                      const QString &clientMsgId = QString());
    // Built-in assets travel as a small JSON body and need no upload (§40.8-
    // 40.9). mediaId keeps "pack/sticker" or the GIF catalog id.
    QString sendSticker(const QString &chatId, const QString &packId,
                        const QString &stickerId, const QString &title,
                        const QString &replyToClientMsgId = QString(),
                        const QString &clientMsgId = QString());
    QString sendGif(const QString &chatId, const QString &gifId, const QString &title,
                    const QString &replyToClientMsgId = QString(),
                    const QString &clientMsgId = QString());
    // §40.10: one message with 1..10 already uploaded media rows.
    QString sendAlbum(const QString &chatId, const QStringList &mediaIds,
                      const QString &caption,
                      const QString &replyToClientMsgId = QString(),
                      const QString &clientMsgId = QString());

    // Encrypted control envelope to one peer (sender key distribution). It is
    // never stored in the message history or the outbox.
    void sendControlMessage(const QString &peerLogin, const QString &kind,
                            const QByteArray &payload, Done<bool> done);

    // True only while the message has not left the device and is inside the
    // cancel window; never reports success for a message that was sent.
    void cancelSend(const QString &clientMsgId, Done<bool> done);
    void deleteLocal(const QString &clientMsgId, Done<bool> done);
    // §37.4: own message inside 48 h, or an admin of the group/channel.
    // Emits messageRemoved and a `message_delete` control event.
    void deleteForEveryone(const QString &clientMsgId, bool asAdmin, Done<bool> done);
    // §37.3: own text message inside 48 h. Re-seals the body, marks it edited
    // and emits `message_edit` to the other participants.
    void editMessage(const QString &clientMsgId, const QString &newBody, Done<bool> done);
    // §37.2: copies one or more messages into target chats and keeps the
    // original author in `forwardedFrom`.
    void forwardMessages(const QStringList &sourceIds, const QStringList &targetChatIds,
                         Done<int> done);
    // §38.2: at most MessagesRepo::kMaxPins per chat.
    void pinMessage(const QString &chatId, const QString &clientMsgId, Done<bool> done);
    void unpinMessage(const QString &chatId, const QString &clientMsgId, Done<bool> done);
    void listPinned(const QString &chatId, Done<QVector<Message>> done);
    void searchInChat(const QString &chatId, const QString &query,
                      Done<QVector<Message>> done);

    static constexpr qint64 kActionWindowMs = 48LL * 60 * 60 * 1000;
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
    void pinsChanged(const QString &chatId);
    // A failed send returns its text so the composer can restore it.
    void messageSendFailed(const QString &clientMsgId, const QString &chatId,
                           const QString &text, const QString &code);
    // A decrypted pairwise control message (sender_key_distribution).
    void controlMessageReceived(const QString &senderLogin, const QString &kind,
                                const QByteArray &payload);
    void pendingChanged(int pending);
    void errorOccurred(const QString &code, const QString &detail);

private:
    QString enqueueMessage(const Message &message);
    void schedulePump(int delayMs);
    void pump();
    void sendHead(const OutboxItem &item);
    void markFailed(const OutboxItem &item, const QString &code);
    void handleSendFailure(const OutboxItem &item, const net::ApiError &error);
    void handleIncoming(const net::Envelope &envelope);
    void handleGroupIncoming(const net::Envelope &envelope, const QString &senderLogin,
                             bool keyPrepared);
    void handleAcknowledged(const QString &clientMsgId, const QString &chatId);
    void handleDelivered(const QString &clientMsgId, const QString &chatId, qint64 at);
    void handleReadReceipt(const QString &chatId, const QStringList &clientMsgIds);
    void sendChatEvent(const QString &chatId, const QString &kind,
                       const QJsonObject &payload);
    void applyIncomingEvent(const QString &kind, const QString &chatId,
                            const QByteArray &plain, const QString &sender);

    storage::StorageWorker *m_storage = nullptr;
    CryptoSession *m_crypto = nullptr;
    GroupCryptoSession *m_groupCrypto = nullptr;
    GroupCryptoSession *m_channelCrypto = nullptr;
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
