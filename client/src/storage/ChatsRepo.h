#pragma once

#include "core/Result.h"
#include "domain/Chat.h"
#include "storage/BodyCipher.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

// Must only be called on the StorageWorker thread.
class ChatsRepo
{
public:
    explicit ChatsRepo(Database &db);

    Result<QVector<Chat>> list() const;
    Result<std::optional<Chat>> get(const QString &chatId) const;
    Result<QString> peerLogin(const QString &chatId) const;
    Result<bool> upsert(const Chat &chat);
    Result<bool> setPinned(const QString &chatId, bool pinned);
    Result<bool> setMuted(const QString &chatId, bool muted);
    // §32.4: 0 clears mute, >0 mutes until that moment, -1 means forever.
    Result<bool> setMutedUntil(const QString &chatId, qint64 untilMs);
    Result<bool> setArchived(const QString &chatId, bool archived);
    Result<bool> remove(const QString &chatId);
    // Deletes the chat row but keeps its messages as orphans (§4.8).
    Result<bool> removeKeepingMessages(const QString &chatId);
    Result<bool> touchLastMessage(const QString &chatId, qint64 ts, const QString &preview,
                                  bool incrementUnread = false);
    // Recomputes preview/last_message_ts/unread from the remaining messages.
    // Used after cancel/delete so the list never shows a removed message.
    Result<bool> recalculateLastMessage(const QString &chatId);

private:
    Database &m_db;
    BodyCipher m_cipher;
};

} // namespace mini::storage
