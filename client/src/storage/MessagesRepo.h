#pragma once

#include "core/Result.h"
#include "domain/Message.h"
#include "storage/BodyCipher.h"

#include <QString>
#include <QVector>

namespace mini::storage {

class Database;

// Must only be called on the StorageWorker thread. Bodies are encrypted with
// DB_KEY on write and decrypted on read; the FTS index uses the plaintext.
struct MessageLookup
{
    bool found = false;
    bool bodyOk = true;
    Message message;
};

class MessagesRepo
{
public:
    explicit MessagesRepo(Database &db);

    // One row by client_msg_id, with the body decrypted when possible.
    Result<MessageLookup> find(const QString &clientMsgId) const;

    Result<bool> insertQueued(const Message &message);
    Result<bool> insertReceived(const Message &message);
    // §49.2: import keeps the state, direction and timestamps from the file.
    // Returns false when (chat_id, client_msg_id) is already stored.
    Result<bool> insertImported(const Message &message);
    Result<bool> updateState(const QString &clientMsgId, MessageState state,
                             qint64 serverTs = 0, const QString &serverMsgId = QString());
    Result<QVector<Message>> listByChat(const QString &chatId, const MessageCursor &before = {},
                                        int limit = 50) const;
    // §49.1: unpaged history for an archive, oldest first.
    Result<QVector<Message>> listAllByChat(const QString &chatId) const;
    // §39.2: the countdown starts when the incoming messages are read. A
    // non-zero timer stamps expires_at on the rows this call marks as read.
    Result<int> markRead(const QString &chatId, qint64 expiresAfterMs = 0,
                         qint64 nowMs = 0);
    Result<bool> deleteLocal(const QString &clientMsgId);
    Result<QVector<Message>> search(const QString &query, int limit = 50) const;
    Result<QVector<Message>> searchInChat(const QString &chatId, const QString &query,
                                          int limit = 100) const;

    // §37.3: re-seals the body and keeps the FTS row in sync.
    Result<bool> updateBody(const QString &clientMsgId, const QString &body,
                            qint64 editedAt);

    // §38.2: at most kMaxPins per chat (enforced by MessageService).
    static constexpr int kMaxPins = 20;
    Result<int> pinCount(const QString &chatId) const;
    Result<bool> setPinned(const QString &chatId, const QString &clientMsgId,
                           qint64 pinnedAt, const QString &pinnedBy);
    Result<bool> unpin(const QString &chatId, const QString &clientMsgId);
    Result<QVector<Message>> pinned(const QString &chatId, int limit = 20) const;

    // §39: outgoing messages start their countdown from the read receipt.
    Result<int> applyReadExpiry(const QStringList &clientMsgIds, qint64 expiresAfterMs,
                                qint64 nowMs);
    // Rows whose expires_at has passed; the cleaner deletes them.
    Result<QVector<Message>> expired(qint64 nowMs) const;
    Result<QVector<Message>> removeExpired(qint64 nowMs);

    // §4.8: the FTS index is rebuilt from decrypted bodies at startup.
    Result<int> rebuildSearchIndex();

private:
    Result<bool> insert(const Message &message);
    // Creates the in-memory FTS table if this connection has not done so yet.
    Result<bool> ensureSearchIndex() const;

    Database &m_db;
    BodyCipher m_cipher;
};

} // namespace mini::storage
