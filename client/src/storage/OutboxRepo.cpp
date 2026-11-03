#include "storage/OutboxRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>

namespace mini::storage {

OutboxRepo::OutboxRepo(Database &db) : m_db(db)
{
}

Result<qint64> OutboxRepo::enqueue(const QString &chatId, const QString &clientMsgId,
                                   qint64 createdAt)
{
    if (chatId.isEmpty() || clientMsgId.isEmpty()) {
        return Result<qint64>::error(
            QStringLiteral("chat_id and client_msg_id are required"));
    }
    detail::Statement statement(
        m_db.handle(),
        "INSERT OR IGNORE INTO outbox(chat_id, client_msg_id, attempts, created_at, "
        "next_attempt_at) VALUES(?, ?, 0, ?, 0)");
    if (!statement.isOk()) {
        return Result<qint64>::error(detail::sqlError(m_db.handle(), "outbox.enqueue"));
    }
    statement.bindText(1, chatId);
    statement.bindText(2, clientMsgId);
    statement.bindInt64(3, createdAt > 0 ? createdAt : QDateTime::currentMSecsSinceEpoch());
    if (statement.step() != SQLITE_DONE) {
        return Result<qint64>::error(detail::sqlError(m_db.handle(), "outbox.enqueue"));
    }
    if (sqlite3_changes(m_db.handle()) == 0) {
        return Result<qint64>::ok(0);
    }
    return Result<qint64>::ok(sqlite3_last_insert_rowid(m_db.handle()));
}

Result<std::optional<OutboxItem>> OutboxRepo::next(const QString &chatId) const
{
    // The head of the chat queue is the lowest id, always. A delayed head
    // blocks the chat instead of letting newer messages overtake it.
    detail::Statement statement(
        m_db.handle(),
        "SELECT id, chat_id, client_msg_id, attempts, created_at, next_attempt_at "
        "FROM outbox WHERE chat_id = ? ORDER BY id ASC LIMIT 1");
    if (!statement.isOk()) {
        return Result<std::optional<OutboxItem>>::error(
            detail::sqlError(m_db.handle(), "outbox.next"));
    }
    statement.bindText(1, chatId);

    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<std::optional<OutboxItem>>::ok(std::nullopt);
    }
    if (rc != SQLITE_ROW) {
        return Result<std::optional<OutboxItem>>::error(
            detail::sqlError(m_db.handle(), "outbox.next"));
    }

    OutboxItem item;
    item.id = statement.int64(0);
    item.chatId = statement.text(1);
    item.clientMsgId = statement.text(2);
    item.attempts = statement.integer(3);
    item.createdAt = statement.int64(4);
    item.nextAttemptAt = statement.int64(5);
    if (item.nextAttemptAt > QDateTime::currentMSecsSinceEpoch()) {
        return Result<std::optional<OutboxItem>>::ok(std::nullopt);
    }
    return Result<std::optional<OutboxItem>>::ok(item);
}

Result<bool> OutboxRepo::markSent(qint64 id)
{
    return remove(id);
}

Result<bool> OutboxRepo::retryLater(qint64 id, qint64 delayMs)
{
    detail::Statement statement(
        m_db.handle(),
        "UPDATE outbox SET attempts = attempts + 1, next_attempt_at = ? WHERE id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.retryLater"));
    }
    statement.bindInt64(1, QDateTime::currentMSecsSinceEpoch() + qMax<qint64>(0, delayMs));
    statement.bindInt64(2, id);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.retryLater"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> OutboxRepo::removeByClientMsgId(const QString &clientMsgId)
{
    detail::Statement statement(m_db.handle(),
                                "DELETE FROM outbox WHERE client_msg_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.removeByClient"));
    }
    statement.bindText(1, clientMsgId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.removeByClient"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> OutboxRepo::remove(qint64 id)
{
    detail::Statement statement(m_db.handle(), "DELETE FROM outbox WHERE id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.remove"));
    }
    statement.bindInt64(1, id);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "outbox.remove"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

} // namespace mini::storage
