#include "services/PollService.h"

#include "core/Clock.h"
#include "domain/Channel.h"
#include "domain/Group.h"
#include "services/MessageService.h"
#include "storage/ChannelsRepo.h"
#include "storage/Database.h"
#include "storage/GroupsRepo.h"
#include "storage/PollsRepo.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace mini::services {

namespace {

template <typename T>
void watch(QObject *owner, QFuture<T> future, std::function<void(T)> done)
{
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
                         T value = watcher->result();
                         watcher->deleteLater();
                         done(std::move(value));
                     });
    watcher->setFuture(future);
}

struct VoteOutcome
{
    int status = -1; // 1 changed, 0 no change, -1 rejected
    QString pollId;
    QString chatId;
    QString error;
    bool removed = false;
    int option = -1;
};

} // namespace

PollService::PollService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void PollService::setMessageService(MessageService *messages)
{
    if (m_messages == messages) {
        return;
    }
    if (m_messages) {
        disconnect(m_messages, nullptr, this, nullptr);
    }
    m_messages = messages;
    if (!m_messages) {
        return;
    }
    if (m_selfLogin.isEmpty()) {
        m_selfLogin = m_messages->selfLogin();
    }
    connect(m_messages, &MessageService::controlMessageReceived, this,
            [this](const QString &sender, const QString &kind, const QByteArray &payload) {
                const QJsonObject object = QJsonDocument::fromJson(payload).object();
                if (kind == QLatin1String("poll_vote")) {
                    const QString pollId = object.value(QStringLiteral("poll_id")).toString();
                    if (pollId.isEmpty()) {
                        return;
                    }
                    applyVoteEvent(sender, object);
                } else if (kind == QLatin1String("poll_close")) {
                    applyCloseEvent(object);
                }
            });
}

Result<PollBody> PollService::validateDraft(const PollBody &draft)
{
    PollBody clean;
    clean.pollId = draft.pollId;
    clean.question = draft.question.trimmed();
    clean.anonymous = draft.anonymous;
    clean.multiple = draft.multiple;
    clean.quiz = draft.quiz;
    clean.allowChange = draft.allowChange;
    clean.correctOption = draft.correctOption;
    if (clean.question.isEmpty() || clean.question.size() > kPollQuestionMax) {
        return Result<PollBody>::error(QStringLiteral("poll.question_invalid"));
    }
    QSet<QString> seen;
    for (const QString &raw : draft.options) {
        const QString option = raw.trimmed();
        if (option.isEmpty() || option.size() > kPollOptionMax) {
            return Result<PollBody>::error(QStringLiteral("poll.option_invalid"));
        }
        if (seen.contains(option)) {
            return Result<PollBody>::error(QStringLiteral("poll.option_duplicate"));
        }
        seen.insert(option);
        clean.options.append(option);
    }
    if (clean.options.size() < kPollMinOptions || clean.options.size() > kPollMaxOptions) {
        return Result<PollBody>::error(QStringLiteral("poll.options_count"));
    }
    if (clean.quiz
        && (clean.correctOption < 0 || clean.correctOption >= clean.options.size())) {
        return Result<PollBody>::error(QStringLiteral("poll.correct_required"));
    }
    if (!clean.quiz) {
        clean.correctOption = -1;
    }
    return Result<PollBody>::ok(clean);
}

void PollService::createPoll(const QString &chatId, const PollBody &draft,
                             Done<QString> done)
{
    if (!m_storage || !m_messages) {
        done(Result<QString>::error(QStringLiteral("poll.unavailable")));
        return;
    }
    auto valid = validateDraft(draft);
    if (valid.isError()) {
        done(Result<QString>::error(valid.error()));
        return;
    }
    PollBody clean = valid.value();
    clean.pollId = clean.pollId.isEmpty() ? Ids::clientMsgId() : clean.pollId;

    if (group_ids::isGroupChat(chatId) || channel_ids::isChannelChat(chatId)) {
        // §38.3: local counting over 200 participants is too expensive.
        const bool channel = channel_ids::isChannelChat(chatId);
        const QString container =
            channel ? channel_ids::channelIdFromChat(chatId)
                    : group_ids::groupIdFromChat(chatId);
        auto future = m_storage->submit([container, channel](storage::Database &db) {
            if (channel) {
                auto members = storage::ChannelsRepo(db).members(container);
                return members.isOk() ? static_cast<int>(members.value().size()) : 0;
            }
            auto members = storage::GroupsRepo(db).members(container);
            return members.isOk() ? static_cast<int>(members.value().size()) : 0;
        });
        watch<int>(this, future, [this, chatId, clean, done](int memberCount) {
            if (memberCount > kPollMaxParticipants) {
                const QString code = QStringLiteral("poll.too_many_participants");
                emit errorOccurred(code, QString());
                done(Result<QString>::error(code));
                return;
            }
            const QString messageId = m_messages->sendPoll(chatId, clean);
            if (messageId.isEmpty()) {
                done(Result<QString>::error(QStringLiteral("poll.send_failed")));
                return;
            }
            emit pollChanged(chatId, clean.pollId);
            done(Result<QString>::ok(clean.pollId));
        });
        return;
    }

    const QString messageId = m_messages->sendPoll(chatId, clean);
    if (messageId.isEmpty()) {
        done(Result<QString>::error(QStringLiteral("poll.send_failed")));
        return;
    }
    emit pollChanged(chatId, clean.pollId);
    done(Result<QString>::ok(clean.pollId));
}

void PollService::vote(const QString &pollId, int optionIndex, Done<bool> done)
{
    if (!m_storage || !m_messages || pollId.isEmpty() || optionIndex < 0) {
        done(Result<bool>::error(QStringLiteral("poll.vote_invalid")));
        return;
    }
    const QString self = m_selfLogin;
    auto future = m_storage->submit([pollId, optionIndex, self](storage::Database &db) {
        VoteOutcome outcome;
        outcome.pollId = pollId;
        outcome.option = optionIndex;
        storage::PollsRepo repo(db);
        auto poll = repo.get(pollId);
        if (poll.isError() || !poll.value().has_value()) {
            outcome.error = QStringLiteral("poll.not_found");
            return outcome;
        }
        if (poll.value()->closed) {
            outcome.error = QStringLiteral("poll.closed");
            return outcome;
        }
        if (optionIndex >= poll.value()->options.size()) {
            outcome.error = QStringLiteral("poll.option_invalid");
            return outcome;
        }
        outcome.chatId = poll.value()->chatId;
        auto mine = repo.votes(pollId);
        if (mine.isError()) {
            outcome.error = mine.error();
            return outcome;
        }
        bool hasOption = false;
        bool hasAny = false;
        for (const PollVote &vote : mine.value()) {
            if (vote.voterLogin.compare(self, Qt::CaseInsensitive) != 0) {
                continue;
            }
            hasAny = true;
            if (vote.optionIndex == optionIndex) {
                hasOption = true;
            }
        }
        if (hasOption) {
            repo.removeVote(pollId, self, optionIndex);
            outcome.removed = true;
        } else if (hasAny && !poll.value()->multiple) {
            if (!poll.value()->allowChange) {
                outcome.error = QStringLiteral("poll.vote_locked");
                return outcome;
            }
            repo.clearVotesOf(pollId, self);
            PollVote vote;
            vote.pollId = pollId;
            vote.voterLogin = self;
            vote.optionIndex = optionIndex;
            vote.createdAt = Clock::nowMs();
            repo.insertVote(vote);
        } else {
            PollVote vote;
            vote.pollId = pollId;
            vote.voterLogin = self;
            vote.optionIndex = optionIndex;
            vote.createdAt = Clock::nowMs();
            repo.insertVote(vote);
        }
        outcome.status = 1;
        return outcome;
    });
    watch<VoteOutcome>(this, future, [this, done](VoteOutcome outcome) {
        if (outcome.status != 1) {
            done(Result<bool>::error(outcome.error));
            return;
        }
        emit pollChanged(outcome.chatId, outcome.pollId);
        QJsonObject payload;
        payload.insert(QStringLiteral("poll_id"), outcome.pollId);
        payload.insert(QStringLiteral("option"), outcome.option);
        payload.insert(QStringLiteral("remove"), outcome.removed);
        m_messages->sendControlEvent(outcome.chatId, QStringLiteral("poll_vote"),
                                     payload);
        done(Result<bool>::ok(true));
    });
}

void PollService::closePoll(const QString &pollId, bool asAdmin, Done<bool> done)
{
    if (!m_storage || !m_messages || pollId.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("poll.unavailable")));
        return;
    }
    const QString self = m_selfLogin;
    const qint64 now = Clock::nowMs();
    auto future = m_storage->submit([pollId, asAdmin, self, now](storage::Database &db) {
        VoteOutcome outcome;
        outcome.pollId = pollId;
        storage::PollsRepo repo(db);
        auto poll = repo.get(pollId);
        if (poll.isError() || !poll.value().has_value()) {
            outcome.error = QStringLiteral("poll.not_found");
            return outcome;
        }
        outcome.chatId = poll.value()->chatId;
        if (poll.value()->closed) {
            outcome.status = 0;
            return outcome;
        }
        const bool author =
            poll.value()->createdBy.compare(self, Qt::CaseInsensitive) == 0;
        if (!author && !asAdmin) {
            outcome.error = QStringLiteral("poll.close_forbidden");
            return outcome;
        }
        repo.setClosed(pollId, now);
        outcome.status = 1;
        return outcome;
    });
    watch<VoteOutcome>(this, future, [this, done](VoteOutcome outcome) {
        if (outcome.status < 0) {
            done(Result<bool>::error(outcome.error));
            return;
        }
        if (outcome.status == 1) {
            emit pollChanged(outcome.chatId, outcome.pollId);
            QJsonObject payload;
            payload.insert(QStringLiteral("poll_id"), outcome.pollId);
            m_messages->sendControlEvent(outcome.chatId, QStringLiteral("poll_close"),
                                         payload);
        }
        done(Result<bool>::ok(true));
    });
}

void PollService::results(const QString &pollId, Done<PollResult> done)
{
    if (!m_storage) {
        done(Result<PollResult>::error(QStringLiteral("poll.unavailable")));
        return;
    }
    const QString self = m_selfLogin;
    auto future = m_storage->submit([pollId, self](storage::Database &db) {
        return storage::PollsRepo(db).results(pollId, self);
    });
    watch<Result<PollResult>>(this, future,
                              [done = std::move(done)](Result<PollResult> result) {
                                  done(std::move(result));
                              });
}

void PollService::resultsForMessage(const QString &clientMsgId, Done<PollResult> done)
{
    if (!m_storage) {
        done(Result<PollResult>::error(QStringLiteral("poll.unavailable")));
        return;
    }
    const QString self = m_selfLogin;
    auto future = m_storage->submit([clientMsgId, self](storage::Database &db) {
        auto poll = storage::PollsRepo(db).byMessage(clientMsgId);
        if (poll.isError() || !poll.value().has_value()) {
            return Result<PollResult>::error(QStringLiteral("poll.not_found"));
        }
        return storage::PollsRepo(db).results(poll.value()->pollId, self);
    });
    watch<Result<PollResult>>(this, future,
                              [done = std::move(done)](Result<PollResult> result) {
                                  done(std::move(result));
                              });
}

void PollService::applyVoteEvent(const QString &senderLogin, const QJsonObject &payload)
{
    if (!m_storage || senderLogin.isEmpty()) {
        return;
    }
    const QString pollId = payload.value(QStringLiteral("poll_id")).toString();
    const int option = payload.value(QStringLiteral("option")).toInt(-1);
    const bool remove = payload.value(QStringLiteral("remove")).toBool(false);
    if (pollId.isEmpty() || option < 0) {
        return;
    }
    auto future = m_storage->submit([pollId, option, remove, senderLogin](
                                        storage::Database &db) {
        storage::PollsRepo repo(db);
        auto poll = repo.get(pollId);
        if (poll.isError() || !poll.value().has_value() || poll.value()->closed) {
            return VoteOutcome{};
        }
        VoteOutcome outcome;
        outcome.pollId = pollId;
        outcome.chatId = poll.value()->chatId;
        if (option >= poll.value()->options.size()) {
            return VoteOutcome{};
        }
        if (remove) {
            repo.removeVote(pollId, senderLogin, option);
        } else {
            if (!poll.value()->multiple) {
                repo.clearVotesOf(pollId, senderLogin);
            }
            PollVote vote;
            vote.pollId = pollId;
            vote.voterLogin = senderLogin;
            vote.optionIndex = option;
            vote.createdAt = Clock::nowMs();
            repo.insertVote(vote);
        }
        outcome.status = 1;
        return outcome;
    });
    watch<VoteOutcome>(this, future, [this](VoteOutcome outcome) {
        if (outcome.status == 1) {
            emit pollChanged(outcome.chatId, outcome.pollId);
        }
    });
}

void PollService::applyCloseEvent(const QJsonObject &payload)
{
    const QString pollId = payload.value(QStringLiteral("poll_id")).toString();
    if (!m_storage || pollId.isEmpty()) {
        return;
    }
    const qint64 now = Clock::nowMs();
    auto future = m_storage->submit([pollId, now](storage::Database &db) {
        storage::PollsRepo repo(db);
        auto poll = repo.get(pollId);
        if (poll.isError() || !poll.value().has_value()) {
            return VoteOutcome{};
        }
        VoteOutcome outcome;
        outcome.pollId = pollId;
        outcome.chatId = poll.value()->chatId;
        if (!poll.value()->closed) {
            repo.setClosed(pollId, now);
            outcome.status = 1;
        }
        return outcome;
    });
    watch<VoteOutcome>(this, future, [this](VoteOutcome outcome) {
        if (outcome.status == 1) {
            emit pollChanged(outcome.chatId, outcome.pollId);
        }
    });
}

} // namespace mini::services
