#include "storage/ReactionsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "client_msg_id, chat_id, sender_login, emoji, created_at";

Reaction readReaction(const detail::Statement &statement)
{
    Reaction reaction;
    reaction.clientMsgId = statement.text(0);
    reaction.chatId = statement.text(1);
    reaction.senderLogin = statement.text(2);
    reaction.emoji = statement.text(3);
    reaction.createdAt = statement.int64(4);
    return reaction;
}

} // namespace

ReactionsRepo::ReactionsRepo(Database &db) : m_db(db) {}

Result<bool> ReactionsRepo::set(const Reaction &reaction)
{
    if (reaction.clientMsgId.isEmpty() || reaction.senderLogin.isEmpty()
        || reaction.emoji.isEmpty()) {
        return Result<bool>::error(QStringLiteral("reaction.invalid"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO reactions(client_msg_id, chat_id, sender_login, emoji, created_at)
              VALUES(?, ?, ?, ?, ?)
              ON CONFLICT(client_msg_id, sender_login) DO UPDATE SET
                  emoji = excluded.emoji,
                  chat_id = excluded.chat_id,
                  created_at = excluded.created_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.set"));
    }
    statement.bindText(1, reaction.clientMsgId);
    statement.bindText(2, reaction.chatId);
    statement.bindText(3, reaction.senderLogin);
    statement.bindText(4, reaction.emoji);
    statement.bindInt64(5, reaction.createdAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.set"));
    }
    return Result<bool>::ok(true);
}

Result<bool> ReactionsRepo::remove(const QString &clientMsgId, const QString &senderLogin)
{
    detail::Statement statement(
        m_db.handle(),
        "DELETE FROM reactions WHERE client_msg_id = ? AND sender_login = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.remove"));
    }
    statement.bindText(1, clientMsgId);
    statement.bindText(2, senderLogin);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.remove"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> ReactionsRepo::removeAll(const QString &clientMsgId)
{
    detail::Statement statement(m_db.handle(),
                                "DELETE FROM reactions WHERE client_msg_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.removeAll"));
    }
    statement.bindText(1, clientMsgId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "reactions.removeAll"));
    }
    return Result<bool>::ok(true);
}

Result<QVector<Reaction>> ReactionsRepo::listForChat(const QString &chatId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM reactions WHERE chat_id = ? "
                       "ORDER BY created_at ASC, sender_login ASC")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Reaction>>::error(
            detail::sqlError(m_db.handle(), "reactions.listForChat"));
    }
    statement.bindText(1, chatId);
    QVector<Reaction> reactions;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        reactions.push_back(readReaction(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Reaction>>::error(
            detail::sqlError(m_db.handle(), "reactions.listForChat"));
    }
    return Result<QVector<Reaction>>::ok(reactions);
}

Result<QVector<Reaction>> ReactionsRepo::listForMessage(const QString &clientMsgId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM reactions WHERE client_msg_id = ? "
                       "ORDER BY created_at ASC, sender_login ASC")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Reaction>>::error(
            detail::sqlError(m_db.handle(), "reactions.listForMessage"));
    }
    statement.bindText(1, clientMsgId);
    QVector<Reaction> reactions;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        reactions.push_back(readReaction(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Reaction>>::error(
            detail::sqlError(m_db.handle(), "reactions.listForMessage"));
    }
    return Result<QVector<Reaction>>::ok(reactions);
}

Result<int> ReactionsRepo::distinctCount(const QString &clientMsgId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT COUNT(DISTINCT emoji) FROM reactions WHERE client_msg_id = ?");
    if (!statement.isOk()) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "reactions.distinctCount"));
    }
    statement.bindText(1, clientMsgId);
    if (statement.step() != SQLITE_ROW) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "reactions.distinctCount"));
    }
    return Result<int>::ok(statement.integer(0));
}

} // namespace mini::storage
