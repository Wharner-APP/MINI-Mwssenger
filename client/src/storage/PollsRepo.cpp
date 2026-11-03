#include "storage/PollsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "poll_id, chat_id, message_client_msg_id, question, options_json, anonymous, "
    "multiple, quiz, allow_change, correct_option, closed, created_by, created_at, "
    "closed_at";

QString encodeOptions(const QStringList &options)
{
    QJsonArray array;
    for (const QString &option : options) {
        array.append(option);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList decodeOptions(const QString &json)
{
    QStringList options;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        options.append(value.toString());
    }
    return options;
}

Poll readPoll(const detail::Statement &statement)
{
    Poll poll;
    poll.pollId = statement.text(0);
    poll.chatId = statement.text(1);
    poll.messageClientMsgId = statement.text(2);
    poll.question = statement.text(3);
    poll.options = decodeOptions(statement.text(4));
    poll.anonymous = statement.boolean(5);
    poll.multiple = statement.boolean(6);
    poll.quiz = statement.boolean(7);
    poll.allowChange = statement.boolean(8);
    poll.correctOption = statement.integer(9);
    poll.closed = statement.boolean(10);
    poll.createdBy = statement.text(11);
    poll.createdAt = statement.int64(12);
    poll.closedAt = statement.int64(13);
    return poll;
}

} // namespace

PollsRepo::PollsRepo(Database &db) : m_db(db) {}

Result<bool> PollsRepo::upsert(const Poll &poll)
{
    if (poll.pollId.isEmpty() || poll.question.isEmpty()
        || poll.options.size() < kPollMinOptions || poll.options.size() > kPollMaxOptions) {
        return Result<bool>::error(QStringLiteral("poll.invalid"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO polls(poll_id, chat_id, message_client_msg_id, question,
                                options_json, anonymous, multiple, quiz, allow_change,
                                correct_option, closed, created_by, created_at, closed_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(poll_id) DO UPDATE SET
                  question = excluded.question,
                  options_json = excluded.options_json,
                  closed = excluded.closed,
                  closed_at = excluded.closed_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.upsert"));
    }
    statement.bindText(1, poll.pollId);
    statement.bindText(2, poll.chatId);
    statement.bindText(3, poll.messageClientMsgId);
    statement.bindText(4, poll.question);
    statement.bindText(5, encodeOptions(poll.options));
    statement.bindBool(6, poll.anonymous);
    statement.bindBool(7, poll.multiple);
    statement.bindBool(8, poll.quiz);
    statement.bindBool(9, poll.allowChange);
    statement.bindInt(10, poll.correctOption);
    statement.bindBool(11, poll.closed);
    statement.bindText(12, poll.createdBy);
    statement.bindInt64(13, poll.createdAt);
    statement.bindInt64(14, poll.closedAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<std::optional<Poll>> PollsRepo::get(const QString &pollId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM polls WHERE poll_id = ?")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Poll>>::error(detail::sqlError(m_db.handle(), "polls.get"));
    }
    statement.bindText(1, pollId);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Poll>>::ok(readPoll(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Poll>>::error(detail::sqlError(m_db.handle(), "polls.get"));
    }
    return Result<std::optional<Poll>>::ok(std::nullopt);
}

Result<std::optional<Poll>> PollsRepo::byMessage(const QString &clientMsgId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM polls WHERE message_client_msg_id = ? LIMIT 1")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Poll>>::error(
            detail::sqlError(m_db.handle(), "polls.byMessage"));
    }
    statement.bindText(1, clientMsgId);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Poll>>::ok(readPoll(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Poll>>::error(
            detail::sqlError(m_db.handle(), "polls.byMessage"));
    }
    return Result<std::optional<Poll>>::ok(std::nullopt);
}

Result<bool> PollsRepo::setClosed(const QString &pollId, qint64 closedAt)
{
    detail::Statement statement(
        m_db.handle(), "UPDATE polls SET closed = 1, closed_at = ? WHERE poll_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.setClosed"));
    }
    statement.bindInt64(1, closedAt);
    statement.bindText(2, pollId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.setClosed"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> PollsRepo::insertVote(const PollVote &vote)
{
    if (vote.pollId.isEmpty() || vote.voterLogin.isEmpty() || vote.optionIndex < 0) {
        return Result<bool>::error(QStringLiteral("poll.vote_invalid"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT OR REPLACE INTO poll_votes(poll_id, voter_login, option_index, created_at)
              VALUES(?, ?, ?, ?))sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.insertVote"));
    }
    statement.bindText(1, vote.pollId);
    statement.bindText(2, vote.voterLogin);
    statement.bindInt(3, vote.optionIndex);
    statement.bindInt64(4, vote.createdAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.insertVote"));
    }
    return Result<bool>::ok(true);
}

Result<bool> PollsRepo::removeVote(const QString &pollId, const QString &voterLogin,
                                   int optionIndex)
{
    detail::Statement statement(
        m_db.handle(),
        "DELETE FROM poll_votes WHERE poll_id = ? AND voter_login = ? AND option_index = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.removeVote"));
    }
    statement.bindText(1, pollId);
    statement.bindText(2, voterLogin);
    statement.bindInt(3, optionIndex);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.removeVote"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> PollsRepo::clearVotesOf(const QString &pollId, const QString &voterLogin)
{
    detail::Statement statement(
        m_db.handle(), "DELETE FROM poll_votes WHERE poll_id = ? AND voter_login = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.clearVotesOf"));
    }
    statement.bindText(1, pollId);
    statement.bindText(2, voterLogin);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "polls.clearVotesOf"));
    }
    return Result<bool>::ok(true);
}

Result<QVector<PollVote>> PollsRepo::votes(const QString &pollId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT poll_id, voter_login, option_index, created_at FROM poll_votes "
        "WHERE poll_id = ? ORDER BY created_at ASC");
    if (!statement.isOk()) {
        return Result<QVector<PollVote>>::error(
            detail::sqlError(m_db.handle(), "polls.votes"));
    }
    statement.bindText(1, pollId);
    QVector<PollVote> votes;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        PollVote vote;
        vote.pollId = statement.text(0);
        vote.voterLogin = statement.text(1);
        vote.optionIndex = statement.integer(2);
        vote.createdAt = statement.int64(3);
        votes.push_back(vote);
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<PollVote>>::error(
            detail::sqlError(m_db.handle(), "polls.votes"));
    }
    return Result<QVector<PollVote>>::ok(votes);
}

Result<PollResult> PollsRepo::results(const QString &pollId, const QString &selfLogin) const
{
    auto poll = get(pollId);
    if (poll.isError()) {
        return Result<PollResult>::error(poll.error());
    }
    if (!poll.value().has_value()) {
        return Result<PollResult>::error(QStringLiteral("poll.not_found"));
    }
    auto voteRows = votes(pollId);
    if (voteRows.isError()) {
        return Result<PollResult>::error(voteRows.error());
    }
    PollResult result;
    result.poll = poll.value().value();
    result.counts = QVector<int>(result.poll.options.size(), 0);
    QSet<QString> voters;
    for (const PollVote &vote : voteRows.value()) {
        if (vote.optionIndex >= 0 && vote.optionIndex < result.counts.size()) {
            result.counts[vote.optionIndex] += 1;
            voters.insert(vote.voterLogin.toLower());
            if (vote.voterLogin.compare(selfLogin, Qt::CaseInsensitive) == 0) {
                result.myChoices.append(vote.optionIndex);
            }
        }
    }
    result.totalVoters = voters.size();
    result.canClose = !result.poll.closed
        && (result.poll.createdBy.compare(selfLogin, Qt::CaseInsensitive) == 0);
    return Result<PollResult>::ok(result);
}

} // namespace mini::storage
