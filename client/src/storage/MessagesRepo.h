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
    Result<bool> updateState(const QString &clientMsgId, MessageState state,
                             qint64 serverTs = 0, const QString &serverMsgId = QString());
    Result<QVector<Message>> listByChat(const QString &chatId, const MessageCursor &before = {},
                                        int limit = 50) const;
    Result<int> markRead(const QString &chatId);
    Result<bool> deleteLocal(const QString &clientMsgId);
    Result<QVector<Message>> search(const QString &query, int limit = 50) const;

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
