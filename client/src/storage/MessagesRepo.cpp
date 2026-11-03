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
    "local_ts, server_ts, reply_to";
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
                outgoing, local_ts, server_ts, reply_to)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?))sql");
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

Result<int> MessagesRepo::markRead(const QString &chatId)
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
                            "m.body, m.state, m.outgoing, m.local_ts, m.server_ts, m.reply_to "
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

} // namespace mini::storage
