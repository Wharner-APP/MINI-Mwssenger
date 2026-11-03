#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mini {

// §38.3: a poll is a message body plus a local tally. Votes arrive as
// encrypted control envelopes and are counted on each client; the server
// never sees them.
struct Poll
{
    QString pollId;
    QString chatId;
    QString messageClientMsgId;
    QString question;
    QStringList options;
    bool anonymous = true;
    bool multiple = false;
    bool quiz = false;
    bool allowChange = false;
    int correctOption = -1;
    bool closed = false;
    QString createdBy;
    qint64 createdAt = 0;
    qint64 closedAt = 0;
};

struct PollVote
{
    QString pollId;
    QString voterLogin;
    int optionIndex = -1;
    qint64 createdAt = 0;
};

// What the card needs: option counts, the caller's own choices and whether
// the caller may close the poll.
struct PollResult
{
    Poll poll;
    QVector<int> counts;
    int totalVoters = 0;
    QVector<int> myChoices;
    bool canClose = false;
};

// Wire form carried inside the E2E message body (`{"poll":{...}}`).
struct PollBody
{
    QString pollId;
    QString question;
    QStringList options;
    bool anonymous = true;
    bool multiple = false;
    bool quiz = false;
    bool allowChange = false;
    int correctOption = -1;
};

[[nodiscard]] QByteArray encodePollBody(const PollBody &body);
[[nodiscard]] bool decodePollBody(const QByteArray &bytes, PollBody *body);

// Limits from §38.3.
constexpr int kPollQuestionMax = 255;
constexpr int kPollOptionMax = 100;
constexpr int kPollMinOptions = 2;
constexpr int kPollMaxOptions = 10;
constexpr int kPollMaxParticipants = 200;

} // namespace mini
