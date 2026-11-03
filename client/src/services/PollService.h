#pragma once

#include "core/Result.h"
#include "domain/Poll.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

class MessageService;

// §38.3: poll creation, voting, local tally and closing. Votes travel as
// encrypted control envelopes between participants; the server sees nothing.
class PollService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit PollService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setMessageService(MessageService *messages);
    void setSelfLogin(const QString &login) { m_selfLogin = login; }

    // Question ≤ 255, 2–10 options ≤ 100 each, quiz needs a correct option.
    [[nodiscard]] static Result<PollBody> validateDraft(const PollBody &draft);

    // Creates the poll and posts it as a message. Fails with
    // `poll.too_many_participants` beyond 200 members.
    void createPoll(const QString &chatId, const PollBody &draft, Done<QString> done);

    // Toggles one option. Single-choice polls replace the vote when
    // `allowChange` is on, otherwise a second choice is rejected.
    void vote(const QString &pollId, int optionIndex, Done<bool> done);

    // The author may always close; an admin passes asAdmin=true.
    void closePoll(const QString &pollId, bool asAdmin, Done<bool> done);

    void results(const QString &pollId, Done<PollResult> done);
    void resultsForMessage(const QString &clientMsgId, Done<PollResult> done);

signals:
    void pollChanged(const QString &chatId, const QString &pollId);
    void errorOccurred(const QString &code, const QString &detail);

private:
    void applyVoteEvent(const QString &senderLogin, const QJsonObject &payload);
    void applyCloseEvent(const QJsonObject &payload);

    storage::StorageWorker *m_storage = nullptr;
    MessageService *m_messages = nullptr;
    QString m_selfLogin;
};

} // namespace mini::services
