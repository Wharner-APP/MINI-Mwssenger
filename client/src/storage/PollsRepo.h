#pragma once

#include "core/Result.h"
#include "domain/Poll.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

// §38.3: the poll definition and the votes received so far. Counts are
// computed here from local rows; the server never stores a tally.
class PollsRepo
{
public:
    explicit PollsRepo(Database &db);

    Result<bool> upsert(const Poll &poll);
    Result<std::optional<Poll>> get(const QString &pollId) const;
    Result<std::optional<Poll>> byMessage(const QString &clientMsgId) const;
    Result<bool> setClosed(const QString &pollId, qint64 closedAt);

    Result<bool> insertVote(const PollVote &vote);
    Result<bool> removeVote(const QString &pollId, const QString &voterLogin,
                            int optionIndex);
    Result<bool> clearVotesOf(const QString &pollId, const QString &voterLogin);
    Result<QVector<PollVote>> votes(const QString &pollId) const;
    // Counts options, total voters and the caller's own choices.
    Result<PollResult> results(const QString &pollId, const QString &selfLogin) const;

private:
    Database &m_db;
};

} // namespace mini::storage
