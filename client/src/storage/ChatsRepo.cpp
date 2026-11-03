#include "storage/ChatsRepo.h"

#include "domain/Media.h"
#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "chat_id, kind, peer_login, title, last_message_ts, last_message_preview, "
    "unread_count, pinned, muted, updated_at, muted_until, archived";

Chat readChat(const detail::Statement &statement, const BodyCipher &cipher)
{
    Chat chat;
    chat.chatId = statement.text(0);
    chat.kind = statement.text(1);
    chat.peerLogin = statement.text(2);
    chat.title = statement.text(3);
    chat.lastMessageTs = statement.int64(4);
    const auto preview = cipher.decrypt(statement.blob(5), chatPreviewAad(chat.chatId));
    chat.lastMessagePreview = preview.isOk() ? preview.value() : QString();
    chat.unreadCount = statement.integer(6);
    chat.pinned = statement.boolean(7);
    chat.mutedUntil = statement.int64(10);
    // §32.4: a timed mute expires on read; muted_until == 0 with muted == 1
    // means "forever".
    const bool muteFlag = statement.boolean(8);
    chat.muted = muteFlag
        && (chat.mutedUntil <= 0
            || chat.mutedUntil > QDateTime::currentMSecsSinceEpoch());
    chat.updatedAt = statement.int64(9);
    chat.archived = statement.boolean(11);
    return chat;
}

} // namespace

ChatsRepo::ChatsRepo(Database &db) : m_db(db), m_cipher(db.dbKey())
{
}

Result<QVector<Chat>> ChatsRepo::list() const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM chats "
                       "ORDER BY pinned DESC, last_message_ts DESC, chat_id ASC")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Chat>>::error(detail::sqlError(m_db.handle(), "chats.list"));
    }

    QVector<Chat> chats;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        chats.push_back(readChat(statement, m_cipher));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Chat>>::error(detail::sqlError(m_db.handle(), "chats.list"));
    }
    return Result<QVector<Chat>>::ok(chats);
}

Result<std::optional<Chat>> ChatsRepo::get(const QString &chatId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM chats WHERE chat_id = ?")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Chat>>::error(detail::sqlError(m_db.handle(), "chats.get"));
    }
    statement.bindText(1, chatId);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Chat>>::ok(readChat(statement, m_cipher));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Chat>>::error(detail::sqlError(m_db.handle(), "chats.get"));
    }
    return Result<std::optional<Chat>>::ok(std::nullopt);
}

Result<QString> ChatsRepo::peerLogin(const QString &chatId) const
{
    detail::Statement statement(m_db.handle(),
                                "SELECT peer_login FROM chats WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<QString>::error(detail::sqlError(m_db.handle(), "chats.peerLogin"));
    }
    statement.bindText(1, chatId);
    if (statement.step() != SQLITE_ROW) {
        return Result<QString>::ok(QString());
    }
    return Result<QString>::ok(statement.text(0));
}

Result<bool> ChatsRepo::upsert(const Chat &chat)
{
    if (chat.chatId.isEmpty()) {
        return Result<bool>::error(QStringLiteral("chat_id is required"));
    }
    auto sealedPreview = m_cipher.encrypt(chat.lastMessagePreview, chatPreviewAad(chat.chatId));
    if (sealedPreview.isError()) {
        return Result<bool>::error(sealedPreview.error());
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO chats(chat_id, kind, peer_login, title, last_message_ts,
                                last_message_preview, unread_count, pinned, muted, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(chat_id) DO UPDATE SET
                  kind = excluded.kind,
                  peer_login = excluded.peer_login,
                  title = excluded.title,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.upsert"));
    }
    statement.bindText(1, chat.chatId);
    statement.bindText(2, chat.kind);
    statement.bindText(3, chat.peerLogin);
    statement.bindText(4, chat.title);
    statement.bindInt64(5, chat.lastMessageTs);
    statement.bindBlob(6, sealedPreview.value());
    statement.bindInt(7, chat.unreadCount);
    statement.bindBool(8, chat.pinned);
    statement.bindBool(9, chat.muted);
    statement.bindInt64(10, chat.updatedAt);

    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<bool> ChatsRepo::setPinned(const QString &chatId, bool pinned)
{
    detail::Statement statement(m_db.handle(), "UPDATE chats SET pinned = ? WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setPinned"));
    }
    statement.bindBool(1, pinned);
    statement.bindText(2, chatId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setPinned"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ChatsRepo::setMuted(const QString &chatId, bool muted)
{
    detail::Statement statement(m_db.handle(), "UPDATE chats SET muted = ? WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setMuted"));
    }
    statement.bindBool(1, muted);
    statement.bindText(2, chatId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setMuted"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ChatsRepo::setMutedUntil(const QString &chatId, qint64 untilMs)
{
    const bool muted = untilMs != 0;
    const qint64 stored = untilMs > 0 ? untilMs : 0;
    detail::Statement statement(
        m_db.handle(), "UPDATE chats SET muted = ?, muted_until = ? WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setMutedUntil"));
    }
    statement.bindBool(1, muted);
    statement.bindInt64(2, stored);
    statement.bindText(3, chatId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setMutedUntil"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ChatsRepo::setArchived(const QString &chatId, bool archived)
{
    detail::Statement statement(m_db.handle(),
                                "UPDATE chats SET archived = ? WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setArchived"));
    }
    statement.bindBool(1, archived);
    statement.bindText(2, chatId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.setArchived"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ChatsRepo::remove(const QString &chatId)
{
    return m_db.transaction([&](Database &tx) {
        if (auto indexed = tx.exec(QByteArrayLiteral(
                "CREATE VIRTUAL TABLE IF NOT EXISTS temp.messages_fts USING fts5("
                "body, tokenize='unicode61 remove_diacritics 2')"));
            indexed.isError()) {
            return indexed;
        }
        detail::Statement rowids(tx.handle(),
                                 "SELECT rowid FROM messages WHERE chat_id = ?");
        if (!rowids.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove"));
        }
        rowids.bindText(1, chatId);

        QVector<qint64> ids;
        int rc = rowids.step();
        while (rc == SQLITE_ROW) {
            ids.push_back(rowids.int64(0));
            rc = rowids.step();
        }
        if (rc != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove"));
        }

        for (qint64 id : ids) {
            detail::Statement dropFts(tx.handle(),
                                      "DELETE FROM temp.messages_fts WHERE rowid = ?");
            if (!dropFts.isOk()) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.fts"));
            }
            dropFts.bindInt64(1, id);
            if (dropFts.step() != SQLITE_DONE) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.fts"));
            }
        }

        detail::Statement dropMessages(tx.handle(), "DELETE FROM messages WHERE chat_id = ?");
        if (!dropMessages.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.messages"));
        }
        dropMessages.bindText(1, chatId);
        if (dropMessages.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.messages"));
        }

        // A deleted chat must not keep retrying queued sends.
        detail::Statement dropOutbox(tx.handle(), "DELETE FROM outbox WHERE chat_id = ?");
        if (!dropOutbox.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.outbox"));
        }
        dropOutbox.bindText(1, chatId);
        if (dropOutbox.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove.outbox"));
        }

        detail::Statement dropChat(tx.handle(), "DELETE FROM chats WHERE chat_id = ?");
        if (!dropChat.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove"));
        }
        dropChat.bindText(1, chatId);
        if (dropChat.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "chats.remove"));
        }
        return Result<bool>::ok(sqlite3_changes(tx.handle()) > 0);
    });
}

Result<bool> ChatsRepo::touchLastMessage(const QString &chatId, qint64 ts,
                                         const QString &preview, bool incrementUnread)
{
    auto sealedPreview = m_cipher.encrypt(preview, chatPreviewAad(chatId));
    if (sealedPreview.isError()) {
        return Result<bool>::error(sealedPreview.error());
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(UPDATE chats SET
                last_message_preview = CASE WHEN ? >= last_message_ts THEN ? ELSE last_message_preview END,
                last_message_ts = MAX(last_message_ts, ?),
                unread_count = unread_count + ?,
                updated_at = ?
              WHERE chat_id = ?)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.touchLastMessage"));
    }
    statement.bindInt64(1, ts);
    statement.bindBlob(2, sealedPreview.value());
    statement.bindInt64(3, ts);
    statement.bindInt(4, incrementUnread ? 1 : 0);
    statement.bindInt64(5, ts);
    statement.bindText(6, chatId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "chats.touchLastMessage"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ChatsRepo::removeKeepingMessages(const QString &chatId)
{
    // Foreign keys must be disabled for this one row deletion so the messages
    // survive as orphans; reopening the direct chat re-attaches them.
    if (auto off = m_db.exec(QByteArrayLiteral("PRAGMA foreign_keys=OFF"));
        off.isError()) {
        return Result<bool>::error(off.error());
    }
    Result<bool> removed = Result<bool>::ok(false);
    {
        detail::Statement statement(m_db.handle(), "DELETE FROM chats WHERE chat_id = ?");
        if (!statement.isOk()) {
            removed = Result<bool>::error(detail::sqlError(m_db.handle(), "chats.removeKeep"));
        } else {
            statement.bindText(1, chatId);
            if (statement.step() != SQLITE_DONE) {
                removed = Result<bool>::error(
                    detail::sqlError(m_db.handle(), "chats.removeKeep"));
            } else {
                removed = Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
            }
        }
    }
    if (auto on = m_db.exec(QByteArrayLiteral("PRAGMA foreign_keys=ON")); on.isError()) {
        return Result<bool>::error(on.error());
    }
    return removed;
}

Result<bool> ChatsRepo::recalculateLastMessage(const QString &chatId)
{
    qint64 lastTs = 0;
    QString preview;
    {
        detail::Statement latest(
            m_db.handle(),
            "SELECT client_msg_id, body, local_ts, server_ts, kind, media_name"
            " FROM messages WHERE chat_id = ? AND state <> 'deleted_local'"
            " ORDER BY CASE WHEN server_ts > 0 THEN server_ts ELSE local_ts END DESC,"
            " client_msg_id DESC LIMIT 1");
        if (!latest.isOk()) {
            return Result<bool>::error(
                detail::sqlError(m_db.handle(), "chats.recalculate.last"));
        }
        latest.bindText(1, chatId);
        const int rc = latest.step();
        if (rc == SQLITE_ROW) {
            const QString clientMsgId = latest.text(0);
            const QByteArray aad = messageAad(chatId, clientMsgId);
            const auto plain = m_cipher.decrypt(latest.blob(1), aad);
            if (plain.isError()) {
                return Result<bool>::error(QStringLiteral("chats.recalculate.body"));
            }
            preview = mediaPreviewText(mediaKindFromString(latest.text(4)),
                                       latest.text(5), plain.value());
            lastTs = latest.int64(2);
            const qint64 serverTs = latest.int64(3);
            if (serverTs > 0) {
                lastTs = serverTs;
            }
        } else if (rc != SQLITE_DONE) {
            return Result<bool>::error(
                detail::sqlError(m_db.handle(), "chats.recalculate.last"));
        }
    }

    int unread = 0;
    {
        detail::Statement count(
            m_db.handle(),
            "SELECT COUNT(*) FROM messages WHERE chat_id = ? AND outgoing = 0"
            " AND state IN ('received', 'delivered')");
        if (!count.isOk()) {
            return Result<bool>::error(
                detail::sqlError(m_db.handle(), "chats.recalculate.unread"));
        }
        count.bindText(1, chatId);
        if (count.step() == SQLITE_ROW) {
            unread = count.integer(0);
        }
    }

    auto sealed = m_cipher.encrypt(preview, chatPreviewAad(chatId));
    if (sealed.isError()) {
        return Result<bool>::error(sealed.error());
    }
    detail::Statement update(
        m_db.handle(),
        "UPDATE chats SET last_message_ts = ?, last_message_preview = ?,"
        " unread_count = ?, updated_at = ? WHERE chat_id = ?");
    if (!update.isOk()) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "chats.recalculate.update"));
    }
    update.bindInt64(1, lastTs);
    update.bindBlob(2, sealed.value());
    update.bindInt(3, unread);
    update.bindInt64(4, lastTs);
    update.bindText(5, chatId);
    if (update.step() != SQLITE_DONE) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "chats.recalculate.update"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

} // namespace mini::storage
