#include "storage/MessagesRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "client_msg_id, chat_id, server_msg_id, sender_login, body, state, outgoing, "
    "local_ts, server_ts, reply_to, kind, media_id, media_name, media_size, "
    "media_duration_ms, edited_at, forwarded_from, expires_at, poll_id, system";
constexpr auto kEffectiveTs = "CASE WHEN server_ts > 0 THEN server_ts ELSE local_ts END";

// FTS5 rejects raw user input as a query, so every token becomes a quoted
// phrase. Internal quotes are doubled, operators lose their meaning.
QString ftsQuery(const QString &query)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList tokens = query.split(whitespace, Qt::SkipEmptyParts);
    QStringList quoted;
    quoted.reserve(tokens.size());
    for (QString token : tokens) {
        token.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        quoted.push_back(QLatin1Char('"') + token + QLatin1Char('"'));
    }
    return quoted.join(QLatin1Char(' '));
}

Message readMessage(const detail::Statement &statement, int offset = 0)
{
    Message message;
    message.clientMsgId = statement.text(offset + 0);
    message.chatId = statement.text(offset + 1);
    message.serverMsgId = statement.text(offset + 2);
    message.senderLogin = statement.text(offset + 3);
    message.state = messageStateFromString(statement.text(offset + 5));
    message.outgoing = statement.boolean(offset + 6);
    message.localTs = statement.int64(offset + 7);
    message.serverTs = statement.int64(offset + 8);
    message.replyToClientMsgId = statement.text(offset + 9);
    message.kind = mediaKindFromString(statement.text(offset + 10));
    message.mediaId = statement.text(offset + 11);
    message.mediaName = statement.text(offset + 12);
    message.mediaSize = statement.int64(offset + 13);
    message.mediaDurationMs = statement.int64(offset + 14);
    message.editedAt = statement.int64(offset + 15);
    message.forwardedFrom = statement.text(offset + 16);
    message.expiresAt = statement.int64(offset + 17);
    message.pollId = statement.text(offset + 18);
    message.system = statement.boolean(offset + 19);
    return message;
}

} // namespace

MessagesRepo::MessagesRepo(Database &db) : m_db(db), m_cipher(db.dbKey())
{
}

Result<bool> MessagesRepo::ensureSearchIndex() const
{
    return m_db.exec(QByteArrayLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS temp.messages_fts USING fts5("
        "body, tokenize='unicode61 remove_diacritics 2')"));
}

Result<MessageLookup> MessagesRepo::find(const QString &clientMsgId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM messages WHERE client_msg_id = ?")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<MessageLookup>::error(detail::sqlError(m_db.handle(), "messages.find"));
    }
    statement.bindText(1, clientMsgId);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<MessageLookup>::ok(MessageLookup{});
    }
    if (rc != SQLITE_ROW) {
        return Result<MessageLookup>::error(detail::sqlError(m_db.handle(), "messages.find"));
    }
    MessageLookup lookup;
    lookup.found = true;
    lookup.message = readMessage(statement);
    const auto plain =
        m_cipher.decrypt(statement.blob(4), messageAad(lookup.message.chatId, clientMsgId));
    if (plain.isOk()) {
        lookup.message.body = plain.value();
    } else {
        lookup.bodyOk = false;
    }
    return Result<MessageLookup>::ok(lookup);
}

Result<bool> MessagesRepo::insertQueued(const Message &message)
{
    Message queued = message;
    queued.state = MessageState::Queued;
    queued.outgoing = true;
    if (queued.localTs == 0) {
        queued.localTs = QDateTime::currentMSecsSinceEpoch();
    }
    return insert(queued);
}

Result<bool> MessagesRepo::insertReceived(const Message &message)
{
    Message received = message;
    received.state = MessageState::Received;
    received.outgoing = false;
    return insert(received);
}

Result<bool> MessagesRepo::insertImported(const Message &message)
{
    return insert(message);
}

Result<bool> MessagesRepo::insert(const Message &message)
{
    if (message.clientMsgId.isEmpty() || message.chatId.isEmpty()) {
        return Result<bool>::error(QStringLiteral("client_msg_id and chat_id are required"));
    }

    const QByteArray aad = messageAad(message.chatId, message.clientMsgId);
    auto sealed = m_cipher.encrypt(message.body, aad);
    if (sealed.isError()) {
        return Result<bool>::error(sealed.error());
    }

    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT OR IGNORE INTO messages(
                client_msg_id, chat_id, server_msg_id, sender_login, body, state,
                outgoing, local_ts, server_ts, reply_to, kind, media_id,
                media_name, media_size, media_duration_ms, edited_at,
                forwarded_from, expires_at, poll_id, system)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert"));
    }
    statement.bindText(1, message.clientMsgId);
    statement.bindText(2, message.chatId);
    statement.bindTextOrNull(3, message.serverMsgId);
    statement.bindText(4, message.senderLogin);
    statement.bindBlob(5, sealed.value());
    statement.bindText(6, messageStateToString(message.state));
    statement.bindBool(7, message.outgoing);
    statement.bindInt64(8, message.localTs);
    statement.bindInt64(9, message.serverTs);
    statement.bindTextOrNull(10, message.replyToClientMsgId);
    statement.bindText(11, mediaKindToString(message.kind));
    statement.bindText(12, message.mediaId);
    statement.bindText(13, message.mediaName);
    statement.bindInt64(14, message.mediaSize);
    statement.bindInt64(15, message.mediaDurationMs);
    statement.bindInt64(16, message.editedAt);
    statement.bindText(17, message.forwardedFrom);
    statement.bindInt64(18, message.expiresAt);
    statement.bindText(19, message.pollId);
    statement.bindBool(20, message.system);

    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert"));
    }
    if (sqlite3_changes(m_db.handle()) == 0) {
        // Duplicate client_msg_id or (chat_id, server_msg_id): already stored.
        return Result<bool>::ok(false);
    }

    detail::Statement rowidStatement(
        m_db.handle(), "SELECT rowid FROM messages WHERE client_msg_id = ?");
    if (!rowidStatement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert.rowid"));
    }
    rowidStatement.bindText(1, message.clientMsgId);
    if (rowidStatement.step() != SQLITE_ROW) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert.rowid"));
    }
    const qint64 rowid = rowidStatement.int64(0);

    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return indexed;
    }

    detail::Statement fts(m_db.handle(),
                          "INSERT INTO temp.messages_fts(rowid, body) VALUES(?, ?)");
    if (!fts.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert.fts"));
    }
    fts.bindInt64(1, rowid);
    fts.bindText(2, message.body);
    if (fts.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.insert.fts"));
    }
    return Result<bool>::ok(true);
}

Result<bool> MessagesRepo::updateState(const QString &clientMsgId, MessageState state,
                                       qint64 serverTs, const QString &serverMsgId)
{
    detail::Statement statement(
        m_db.handle(),
        R"sql(UPDATE messages SET
                state = ?,
                server_ts = CASE WHEN ? > 0 THEN ? ELSE server_ts END,
                server_msg_id = CASE WHEN ? <> '' THEN ? ELSE server_msg_id END
              WHERE client_msg_id = ?)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.updateState"));
    }
    statement.bindText(1, messageStateToString(state));
    statement.bindInt64(2, serverTs);
    statement.bindInt64(3, serverTs);
    statement.bindText(4, serverMsgId);
    statement.bindText(5, serverMsgId);
    statement.bindText(6, clientMsgId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "messages.updateState"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<QVector<Message>> MessagesRepo::listByChat(const QString &chatId,
                                                  const MessageCursor &before,
                                                  int limit) const
{
    const int pageSize = qBound(1, limit, 500);
    QString sql = QStringLiteral("SELECT %1 FROM messages WHERE chat_id = ?")
                      .arg(QString::fromLatin1(kColumns));
    if (before.isValid()) {
        sql += QStringLiteral(" AND (%1 < ? OR (%1 = ? AND client_msg_id < ?))")
                   .arg(QString::fromLatin1(kEffectiveTs));
    }
    sql += QStringLiteral(" ORDER BY %1 DESC, client_msg_id DESC LIMIT ?")
               .arg(QString::fromLatin1(kEffectiveTs));

    detail::Statement statement(m_db.handle(), sql.toUtf8().constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.listByChat"));
    }
    int index = 1;
    statement.bindText(index++, chatId);
    if (before.isValid()) {
        statement.bindInt64(index++, before.ts);
        statement.bindInt64(index++, before.ts);
        statement.bindText(index++, before.clientMsgId);
    }
    statement.bindInt(index++, pageSize);

    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        auto body = m_cipher.decrypt(statement.blob(4), messageAad(message.chatId, message.clientMsgId));
        if (body.isError()) {
            return Result<QVector<Message>>::error(
                QStringLiteral("cannot decrypt message %1").arg(message.clientMsgId));
        }
        message.body = body.value();
        messages.push_front(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.listByChat"));
    }
    return Result<QVector<Message>>::ok(messages);
}

Result<QVector<Message>> MessagesRepo::listAllByChat(const QString &chatId) const
{
    const QString sql = QStringLiteral(
                            "SELECT %1 FROM messages WHERE chat_id = ? "
                            "AND state <> 'deleted_local' "
                            "ORDER BY %2 ASC, client_msg_id ASC")
                            .arg(QString::fromLatin1(kColumns),
                                 QString::fromLatin1(kEffectiveTs));

    detail::Statement statement(m_db.handle(), sql.toUtf8().constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.listAllByChat"));
    }
    statement.bindText(1, chatId);

    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        auto body = m_cipher.decrypt(statement.blob(4),
                                     messageAad(message.chatId, message.clientMsgId));
        if (body.isError()) {
            return Result<QVector<Message>>::error(
                QStringLiteral("cannot decrypt message %1").arg(message.clientMsgId));
        }
        message.body = body.value();
        messages.push_back(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.listAllByChat"));
    }
    return Result<QVector<Message>>::ok(messages);
}

Result<int> MessagesRepo::markRead(const QString &chatId, qint64 expiresAfterMs, qint64 nowMs)
{
    int updated = 0;
    auto result = m_db.transaction([&](Database &tx) -> Result<bool> {
        detail::Statement reset(tx.handle(),
                                "UPDATE chats SET unread_count = 0 WHERE chat_id = ?");
        if (!reset.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.markRead"));
        }
        reset.bindText(1, chatId);
        if (reset.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.markRead"));
        }

        // §39.2: reading starts the countdown. Stamp the rows this call is
        // about to flip from received/delivered so older read messages keep
        // their original deadline (or none).
        const bool timer = expiresAfterMs > 0;
        if (timer) {
            const qint64 now = nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
            detail::Statement expiry(
                tx.handle(),
                "UPDATE messages SET expires_at = ? WHERE chat_id = ? AND outgoing = 0 "
                "AND expires_at = 0 AND state IN ('received', 'delivered')");
            if (!expiry.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "messages.markRead.expiry"));
            }
            expiry.bindInt64(1, now + expiresAfterMs);
            expiry.bindText(2, chatId);
            if (expiry.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "messages.markRead.expiry"));
            }
        }

        detail::Statement incoming(
            tx.handle(),
            "UPDATE messages SET state = 'read' WHERE chat_id = ? AND outgoing = 0 "
            "AND state IN ('received', 'delivered')");
        if (!incoming.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.markRead"));
        }
        incoming.bindText(1, chatId);
        if (incoming.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.markRead"));
        }
        updated = sqlite3_changes(tx.handle());
        return Result<bool>::ok(true);
    });
    if (result.isError()) {
        return Result<int>::error(result.error());
    }
    return Result<int>::ok(updated);
}

Result<int> MessagesRepo::applyReadExpiry(const QStringList &clientMsgIds,
                                          qint64 expiresAfterMs, qint64 nowMs)
{
    if (expiresAfterMs <= 0 || clientMsgIds.isEmpty()) {
        return Result<int>::ok(0);
    }
    const qint64 now = nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
    int updated = 0;
    for (const QString &clientMsgId : clientMsgIds) {
        detail::Statement statement(
            m_db.handle(),
            "UPDATE messages SET expires_at = ? WHERE client_msg_id = ? AND expires_at = 0");
        if (!statement.isOk()) {
            return Result<int>::error(
                detail::sqlError(m_db.handle(), "messages.applyReadExpiry"));
        }
        statement.bindInt64(1, now + expiresAfterMs);
        statement.bindText(2, clientMsgId);
        if (statement.step() != SQLITE_DONE) {
            return Result<int>::error(
                detail::sqlError(m_db.handle(), "messages.applyReadExpiry"));
        }
        updated += sqlite3_changes(m_db.handle());
    }
    return Result<int>::ok(updated);
}

Result<QVector<Message>> MessagesRepo::expired(qint64 nowMs) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM messages WHERE expires_at > 0 AND expires_at <= ? "
                       "ORDER BY expires_at ASC LIMIT 500")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.expired"));
    }
    statement.bindInt64(1, nowMs);
    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        const auto body = m_cipher.decrypt(statement.blob(4),
                                           messageAad(message.chatId, message.clientMsgId));
        if (body.isOk()) {
            message.body = body.value();
        }
        messages.push_back(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.expired"));
    }
    return Result<QVector<Message>>::ok(messages);
}

Result<QVector<Message>> MessagesRepo::removeExpired(qint64 nowMs)
{
    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return Result<QVector<Message>>::error(indexed.error());
    }
    QVector<Message> removed;
    auto result = m_db.transaction([&](Database &tx) -> Result<bool> {
        detail::Statement select(
            tx.handle(),
            "SELECT rowid, client_msg_id, chat_id, media_id FROM messages "
            "WHERE expires_at > 0 AND expires_at <= ?");
        if (!select.isOk()) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.removeExpired"));
        }
        select.bindInt64(1, nowMs);
        int rc = select.step();
        while (rc == SQLITE_ROW) {
            Message message;
            message.clientMsgId = select.text(1);
            message.chatId = select.text(2);
            message.mediaId = select.text(3);
            message.expiresAt = nowMs;
            removed.push_back(message);

            detail::Statement fts(tx.handle(),
                                  "DELETE FROM temp.messages_fts WHERE rowid = ?");
            if (!fts.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "messages.removeExpired.fts"));
            }
            fts.bindInt64(1, select.int64(0));
            if (fts.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "messages.removeExpired.fts"));
            }
            rc = select.step();
        }
        if (rc != SQLITE_DONE) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.removeExpired"));
        }
        detail::Statement drop(
            tx.handle(), "DELETE FROM messages WHERE expires_at > 0 AND expires_at <= ?");
        if (!drop.isOk()) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.removeExpired"));
        }
        drop.bindInt64(1, nowMs);
        if (drop.step() != SQLITE_DONE) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.removeExpired"));
        }
        return Result<bool>::ok(true);
    });
    if (result.isError()) {
        return Result<QVector<Message>>::error(result.error());
    }
    return Result<QVector<Message>>::ok(removed);
}

Result<bool> MessagesRepo::deleteLocal(const QString &clientMsgId)
{
    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return indexed;
    }
    return m_db.transaction([&](Database &tx) -> Result<bool> {
        detail::Statement row(tx.handle(),
                              "SELECT rowid, chat_id FROM messages WHERE client_msg_id = ?");
        if (!row.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.deleteLocal"));
        }
        row.bindText(1, clientMsgId);
        if (row.step() != SQLITE_ROW) {
            return Result<bool>::ok(false);
        }
        const qint64 rowid = row.int64(0);
        const QString chatId = row.text(1);

        detail::Statement dropFts(tx.handle(), "DELETE FROM temp.messages_fts WHERE rowid = ?");
        if (!dropFts.isOk()) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.deleteLocal.fts"));
        }
        dropFts.bindInt64(1, rowid);
        if (dropFts.step() != SQLITE_DONE) {
            return Result<bool>::error(
                detail::sqlError(tx.handle(), "messages.deleteLocal.fts"));
        }

        auto empty = m_cipher.encrypt(QString(), messageAad(chatId, clientMsgId));
        if (empty.isError()) {
            return Result<bool>::error(empty.error());
        }
        detail::Statement update(
            tx.handle(), "UPDATE messages SET state = 'deleted_local', body = ? "
                         "WHERE client_msg_id = ?");
        if (!update.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.deleteLocal"));
        }
        update.bindBlob(1, empty.value());
        update.bindText(2, clientMsgId);
        if (update.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.deleteLocal"));
        }
        return Result<bool>::ok(true);
    });
}

Result<int> MessagesRepo::rebuildSearchIndex()
{
    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return Result<int>::error(indexed.error());
    }

    struct Row
    {
        qint64 rowid;
        QString clientMsgId;
        QString chatId;
        QByteArray sealed;
    };

    QVector<Row> rows;
    {
        detail::Statement select(
            m_db.handle(),
            "SELECT rowid, client_msg_id, chat_id, body FROM messages "
            "WHERE state <> 'deleted_local'");
        if (!select.isOk()) {
            return Result<int>::error(detail::sqlError(m_db.handle(), "messages.reindex"));
        }
        int rc = select.step();
        while (rc == SQLITE_ROW) {
            rows.push_back(Row{select.int64(0), select.text(1), select.text(2), select.blob(3)});
            rc = select.step();
        }
        if (rc != SQLITE_DONE) {
            return Result<int>::error(detail::sqlError(m_db.handle(), "messages.reindex"));
        }
    }

    auto rebuilt = m_db.transaction([&](Database &tx) -> Result<bool> {
        if (auto cleared = tx.exec(QByteArrayLiteral("DELETE FROM temp.messages_fts"));
            cleared.isError()) {
            return cleared;
        }
        for (const Row &row : rows) {
            auto body = m_cipher.decrypt(row.sealed, messageAad(row.chatId, row.clientMsgId));
            if (body.isError()) {
                return Result<bool>::error(body.error());
            }
            detail::Statement insert(
                tx.handle(), "INSERT INTO temp.messages_fts(rowid, body) VALUES(?, ?)");
            if (!insert.isOk()) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "messages.reindex"));
            }
            insert.bindInt64(1, row.rowid);
            insert.bindText(2, body.value());
            if (insert.step() != SQLITE_DONE) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "messages.reindex"));
            }
        }
        return Result<bool>::ok(true);
    });
    if (rebuilt.isError()) {
        return Result<int>::error(rebuilt.error());
    }
    return Result<int>::ok(rows.size());
}

Result<QVector<Message>> MessagesRepo::search(const QString &query, int limit) const
{
    const QString match = ftsQuery(query);
    if (match.isEmpty()) {
        return Result<QVector<Message>>::ok({});
    }

    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return Result<QVector<Message>>::error(indexed.error());
    }

    const int pageSize = qBound(1, limit, 500);
    const QString sql = QStringLiteral(
                            "SELECT m.client_msg_id, m.chat_id, m.server_msg_id, m.sender_login, "
                            "m.body, m.state, m.outgoing, m.local_ts, m.server_ts, m.reply_to, "
                            "m.kind, m.media_id, m.media_name, m.media_size, "
                            "m.media_duration_ms "
                            "FROM temp.messages_fts JOIN messages m "
                            "ON m.rowid = temp.messages_fts.rowid "
                            "WHERE messages_fts MATCH ? AND m.state <> 'deleted_local' "
                            "ORDER BY rank LIMIT ?");
    detail::Statement statement(m_db.handle(), sql.toUtf8().constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(detail::sqlError(m_db.handle(), "messages.search"));
    }
    statement.bindText(1, match);
    statement.bindInt(2, pageSize);

    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        auto body = m_cipher.decrypt(statement.blob(4), messageAad(message.chatId, message.clientMsgId));
        if (body.isError()) {
            return Result<QVector<Message>>::error(
                QStringLiteral("cannot decrypt message %1").arg(message.clientMsgId));
        }
        message.body = body.value();
        messages.push_back(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(detail::sqlError(m_db.handle(), "messages.search"));
    }
    return Result<QVector<Message>>::ok(messages);
}

Result<QVector<Message>> MessagesRepo::searchInChat(const QString &chatId,
                                                    const QString &query, int limit) const
{
    const QString match = ftsQuery(query);
    if (match.isEmpty() || chatId.isEmpty()) {
        return Result<QVector<Message>>::ok({});
    }
    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return Result<QVector<Message>>::error(indexed.error());
    }
    const int pageSize = qBound(1, limit, 500);
    const QString sql = QStringLiteral(
        "SELECT m.client_msg_id, m.chat_id, m.server_msg_id, m.sender_login, m.body, "
        "m.state, m.outgoing, m.local_ts, m.server_ts, m.reply_to, m.kind, m.media_id, "
        "m.media_name, m.media_size, m.media_duration_ms, m.edited_at, m.forwarded_from "
        "FROM temp.messages_fts JOIN messages m ON m.rowid = temp.messages_fts.rowid "
        "WHERE messages_fts MATCH ? AND m.chat_id = ? AND m.state <> 'deleted_local' "
        "ORDER BY rank LIMIT ?");
    detail::Statement statement(m_db.handle(), sql.toUtf8().constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.searchInChat"));
    }
    statement.bindText(1, match);
    statement.bindText(2, chatId);
    statement.bindInt(3, pageSize);

    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        auto body =
            m_cipher.decrypt(statement.blob(4), messageAad(message.chatId, message.clientMsgId));
        if (body.isError()) {
            return Result<QVector<Message>>::error(
                QStringLiteral("cannot decrypt message %1").arg(message.clientMsgId));
        }
        message.body = body.value();
        messages.push_back(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(
            detail::sqlError(m_db.handle(), "messages.searchInChat"));
    }
    return Result<QVector<Message>>::ok(messages);
}

Result<bool> MessagesRepo::updateBody(const QString &clientMsgId, const QString &body,
                                      qint64 editedAt)
{
    if (auto indexed = ensureSearchIndex(); indexed.isError()) {
        return indexed;
    }
    return m_db.transaction([&](Database &tx) -> Result<bool> {
        detail::Statement row(tx.handle(),
                              "SELECT rowid, chat_id FROM messages WHERE client_msg_id = ?");
        if (!row.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.updateBody"));
        }
        row.bindText(1, clientMsgId);
        if (row.step() != SQLITE_ROW) {
            return Result<bool>::ok(false);
        }
        const qint64 rowid = row.int64(0);
        const QString chatId = row.text(1);
        const auto sealed = m_cipher.encrypt(body, messageAad(chatId, clientMsgId));
        if (sealed.isError()) {
            return Result<bool>::error(sealed.error());
        }
        detail::Statement update(
            tx.handle(),
            "UPDATE messages SET body = ?, edited_at = ? WHERE client_msg_id = ?");
        if (!update.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.updateBody"));
        }
        update.bindBlob(1, sealed.value());
        update.bindInt64(2, editedAt);
        update.bindText(3, clientMsgId);
        if (update.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.updateBody"));
        }
        detail::Statement fts(
            tx.handle(), "UPDATE temp.messages_fts SET body = ? WHERE rowid = ?");
        if (!fts.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.updateBody.fts"));
        }
        fts.bindText(1, body);
        fts.bindInt64(2, rowid);
        if (fts.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "messages.updateBody.fts"));
        }
        return Result<bool>::ok(true);
    });
}

Result<int> MessagesRepo::pinCount(const QString &chatId) const
{
    detail::Statement statement(m_db.handle(),
                                "SELECT COUNT(*) FROM chat_pins WHERE chat_id = ?");
    if (!statement.isOk()) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "pins.count"));
    }
    statement.bindText(1, chatId);
    if (statement.step() != SQLITE_ROW) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "pins.count"));
    }
    return Result<int>::ok(statement.integer(0));
}

Result<bool> MessagesRepo::setPinned(const QString &chatId, const QString &clientMsgId,
                                     qint64 pinnedAt, const QString &pinnedBy)
{
    detail::Statement statement(
        m_db.handle(),
        "INSERT OR REPLACE INTO chat_pins(chat_id, client_msg_id, pinned_at, pinned_by) "
        "VALUES(?, ?, ?, ?)");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "pins.set"));
    }
    statement.bindText(1, chatId);
    statement.bindText(2, clientMsgId);
    statement.bindInt64(3, pinnedAt);
    statement.bindText(4, pinnedBy);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "pins.set"));
    }
    return Result<bool>::ok(true);
}

Result<bool> MessagesRepo::unpin(const QString &chatId, const QString &clientMsgId)
{
    detail::Statement statement(
        m_db.handle(),
        "DELETE FROM chat_pins WHERE chat_id = ? AND client_msg_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "pins.unpin"));
    }
    statement.bindText(1, chatId);
    statement.bindText(2, clientMsgId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "pins.unpin"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<QVector<Message>> MessagesRepo::pinned(const QString &chatId, int limit) const
{
    QString columns = QString::fromLatin1(kColumns);
    columns.replace(QStringLiteral(", "), QStringLiteral(", m."));
    columns.prepend(QStringLiteral("m."));
    const QString sql =
        QStringLiteral("SELECT %1, p.pinned_at FROM chat_pins p JOIN messages m "
                       "ON m.client_msg_id = p.client_msg_id "
                       "WHERE p.chat_id = ? AND m.state <> 'deleted_local' "
                       "ORDER BY p.pinned_at DESC LIMIT ?")
            .arg(columns);
    detail::Statement statement(m_db.handle(), sql.toUtf8().constData());
    if (!statement.isOk()) {
        return Result<QVector<Message>>::error(detail::sqlError(m_db.handle(), "pins.list"));
    }
    statement.bindText(1, chatId);
    statement.bindInt(2, qBound(1, limit, MessagesRepo::kMaxPins));
    QVector<Message> messages;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        Message message = readMessage(statement);
        auto body =
            m_cipher.decrypt(statement.blob(4), messageAad(message.chatId, message.clientMsgId));
        if (body.isError()) {
            continue;
        }
        message.body = body.value();
        message.pinnedAt = statement.int64(17);
        messages.push_back(message);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Message>>::error(detail::sqlError(m_db.handle(), "pins.list"));
    }
    return Result<QVector<Message>>::ok(messages);
}

} // namespace mini::storage
