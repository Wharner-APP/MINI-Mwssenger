#include "core/Clock.h"
#include "services/MessageService.h"
#include "services/PollService.h"
#include "storage/PollsRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

template <typename T>
T waitFor(std::function<void(std::function<void(Result<T>)>)> start)
{
    bool done = false;
    T value{};
    start([&](Result<T> result) {
        done = true;
        if (result.isOk()) {
            value = result.value();
        }
    });
    for (int i = 0; i < 500 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return value;
}

QString waitError(std::function<void(std::function<void(Result<bool>)>)> start)
{
    bool done = false;
    QString error;
    start([&](Result<bool> result) {
        done = true;
        if (result.isError()) {
            error = result.error();
        }
    });
    for (int i = 0; i < 500 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return error;
}

PollBody draft()
{
    PollBody body;
    body.question = QStringLiteral("Where?");
    body.options = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    body.anonymous = true;
    return body;
}

std::optional<PollResult> results(storage::StorageWorker &worker, const QString &pollId)
{
    auto future = worker.submit([pollId](storage::Database &db) {
        return storage::PollsRepo(db).results(pollId, QStringLiteral("me"));
    });
    future.waitForFinished();
    if (future.result().isError()) {
        return std::nullopt;
    }
    return future.result().value();
}

bool waitForPoll(storage::StorageWorker &worker, const QString &pollId)
{
    for (int i = 0; i < 200; ++i) {
        auto future = worker.submit([pollId](storage::Database &db) {
            return storage::PollsRepo(db).get(pollId).isOk();
        });
        future.waitForFinished();
        if (future.result()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return false;
}

} // namespace

class TestPolls : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void validatesQuestionOptionsAndQuiz();
    void createsAndCountsVotesLocally();
    void singleChoiceReplacementAndLock();
    void multipleAnswers();
    void closesPollAndRejectsLateVotes();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::StorageWorker> m_worker;
    std::unique_ptr<MessageService> m_messages;
    std::unique_ptr<PollService> m_polls;
    int m_counter = 0;
};

void TestPolls::init()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<storage::StorageWorker>();
    QVERIFY(m_worker
                ->open(m_dir.filePath(QStringLiteral("polls-%1.db").arg(++m_counter)),
                       dbKey())
                .isOk());
    seedChat(*m_worker, QStringLiteral("chat-a"), QStringLiteral("alice"), 100);
    m_messages = std::make_unique<MessageService>(m_worker.get());
    m_messages->setSelfLogin(QStringLiteral("me"));
    m_polls = std::make_unique<PollService>(m_worker.get());
    m_polls->setSelfLogin(QStringLiteral("me"));
    m_polls->setMessageService(m_messages.get());
}

void TestPolls::cleanup()
{
    m_polls.reset();
    m_messages.reset();
    m_worker.reset();
}

void TestPolls::validatesQuestionOptionsAndQuiz()
{
    PollBody body = draft();
    QVERIFY(PollService::validateDraft(body).isOk());

    body.question = QString(256, QLatin1Char('x'));
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.question_invalid"));
    body.question = QStringLiteral("Q");

    body.options = {QStringLiteral("only")};
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.options_count"));
    body.options.clear();
    for (int i = 0; i < 11; ++i) {
        body.options.append(QStringLiteral("o%1").arg(i));
    }
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.options_count"));

    body = draft();
    body.options = {QStringLiteral("same"), QStringLiteral("same")};
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.option_duplicate"));

    body = draft();
    body.quiz = true;
    body.correctOption = -1;
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.correct_required"));
    body.correctOption = 1;
    QVERIFY(PollService::validateDraft(body).isOk());

    body.options[0] = QString(101, QLatin1Char('x'));
    QCOMPARE(PollService::validateDraft(body).error(),
             QStringLiteral("poll.option_invalid"));
}

void TestPolls::createsAndCountsVotesLocally()
{
    QSignalSpy changed(m_polls.get(), &PollService::pollChanged);
    const QString pollId = waitFor<QString>([this](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), draft(), done);
    });
    QVERIFY(!pollId.isEmpty());
    QVERIFY(waitForPoll(*m_worker, pollId));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 2000);

    const QString error = waitError([this, pollId](auto done) {
        m_polls->vote(pollId, 1, done);
    });
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto tally = results(*m_worker, pollId);
    QVERIFY(tally.has_value());
    QCOMPARE(tally->counts.size(), 3);
    QCOMPARE(tally->counts.at(1), 1);
    QCOMPARE(tally->counts.at(0), 0);
    QCOMPARE(tally->totalVoters, 1);
    QCOMPARE(tally->myChoices, QVector<int>{1});
}

void TestPolls::singleChoiceReplacementAndLock()
{
    const QString pollId = waitFor<QString>([this](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), draft(), done);
    });
    QVERIFY(waitForPoll(*m_worker, pollId));
    QVERIFY(waitError([this, pollId](auto done) {
                m_polls->vote(pollId, 0, done);
            })
                .isEmpty());
    // No allowChange: a second option is rejected.
    QCOMPARE(waitError([this, pollId](auto done) {
                 m_polls->vote(pollId, 1, done);
             }),
             QStringLiteral("poll.vote_locked"));

    PollBody changeable = draft();
    changeable.allowChange = true;
    const QString second = waitFor<QString>([this, changeable](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), changeable, done);
    });
    QVERIFY(waitForPoll(*m_worker, second));
    QVERIFY(waitError([this, second](auto done) {
                m_polls->vote(second, 0, done);
            })
                .isEmpty());
    QVERIFY(waitError([this, second](auto done) {
                m_polls->vote(second, 2, done);
            })
                .isEmpty());
    auto tally = results(*m_worker, second);
    QVERIFY(tally.has_value());
    QCOMPARE(tally->counts.at(0), 0);
    QCOMPARE(tally->counts.at(2), 1);
    QCOMPARE(tally->myChoices, QVector<int>{2});
}

void TestPolls::multipleAnswers()
{
    PollBody multiple = draft();
    multiple.multiple = true;
    const QString pollId = waitFor<QString>([this, multiple](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), multiple, done);
    });
    QVERIFY(waitForPoll(*m_worker, pollId));
    QVERIFY(waitError([this, pollId](auto done) {
                m_polls->vote(pollId, 0, done);
            })
                .isEmpty());
    QVERIFY(waitError([this, pollId](auto done) {
                m_polls->vote(pollId, 2, done);
            })
                .isEmpty());
    auto tally = results(*m_worker, pollId);
    QVERIFY(tally.has_value());
    QCOMPARE(tally->counts.at(0), 1);
    QCOMPARE(tally->counts.at(2), 1);
    QCOMPARE(tally->myChoices.size(), 2);

    // Pressing the same option again removes just that vote.
    QVERIFY(waitError([this, pollId](auto done) {
                m_polls->vote(pollId, 0, done);
            })
                .isEmpty());
    tally = results(*m_worker, pollId);
    QVERIFY(tally.has_value());
    QCOMPARE(tally->counts.at(0), 0);
    QCOMPARE(tally->counts.at(2), 1);
}

void TestPolls::closesPollAndRejectsLateVotes()
{
    const QString pollId = waitFor<QString>([this](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), draft(), done);
    });
    QVERIFY(waitForPoll(*m_worker, pollId));
    QVERIFY(waitError([this, pollId](auto done) {
                m_polls->closePoll(pollId, false, done);
            })
                .isEmpty());
    QCOMPARE(waitError([this, pollId](auto done) {
                 m_polls->vote(pollId, 0, done);
             }),
             QStringLiteral("poll.closed"));

    // A non-author cannot close someone else's poll without admin rights.
    const QString other = waitFor<QString>([this](auto done) {
        m_polls->createPoll(QStringLiteral("chat-a"), draft(), done);
    });
    QVERIFY(waitForPoll(*m_worker, other));
    PollService stranger(m_worker.get());
    stranger.setSelfLogin(QStringLiteral("stranger"));
    stranger.setMessageService(m_messages.get());
    QCOMPARE(waitError([&stranger, other](auto done) {
                 stranger.closePoll(other, false, done);
             }),
             QStringLiteral("poll.close_forbidden"));
    QVERIFY(waitError([&stranger, other](auto done) {
                stranger.closePoll(other, true, done);
            })
                .isEmpty());
}

QTEST_MAIN(TestPolls)
#include "test_polls.moc"
