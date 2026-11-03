#include "core/Clock.h"
#include "domain/Reaction.h"
#include "services/MessageService.h"
#include "storage/ReactionsRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QCoreApplication>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
    for (int i = 0; i < 400 && !done; ++i) {
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
    for (int i = 0; i < 400 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return error;
}

QVector<Reaction> reactions(storage::StorageWorker &worker, const QString &chatId)
{
    auto future = worker.submit([chatId](storage::Database &db) {
        return storage::ReactionsRepo(db).listForChat(chatId);
    });
    future.waitForFinished();
    return future.result().isOk() ? future.result().value() : QVector<Reaction>();
}

} // namespace

class TestReactions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultsAreTwelve();
    void toggleSetsReplacesAndRemoves();
    void distinctLimitIsTwenty();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::StorageWorker> m_worker;
    std::unique_ptr<MessageService> m_service;
    int m_counter = 0;
};

void TestReactions::init()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<storage::StorageWorker>();
    QVERIFY(m_worker
                ->open(m_dir.filePath(QStringLiteral("reactions-%1.db").arg(++m_counter)),
                       dbKey())
                .isOk());
    seedChat(*m_worker, QStringLiteral("chat-a"), QStringLiteral("alice"), 100);
    seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                QStringLiteral("hello"), Clock::nowMs());
    m_service = std::make_unique<MessageService>(m_worker.get());
    m_service->setSelfLogin(QStringLiteral("me"));
}

void TestReactions::cleanup()
{
    m_service.reset();
    m_worker.reset();
}

void TestReactions::defaultsAreTwelve()
{
    QCOMPARE(defaultReactions().size(), 12);
    QCOMPARE(QSet<QString>(defaultReactions().begin(), defaultReactions().end()).size(), 12);
}

void TestReactions::toggleSetsReplacesAndRemoves()
{
    QSignalSpy changed(m_service.get(), &MessageService::reactionsChanged);

    const QString error = waitError([this](auto done) {
        m_service->toggleReaction(QStringLiteral("m1"), QStringLiteral("👍"), done);
    });
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reactions(*m_worker, QStringLiteral("chat-a")).size(), 1);
    QCOMPARE(reactions(*m_worker, QStringLiteral("chat-a")).first().emoji,
             QStringLiteral("👍"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 2000);

    // The same emoji removes the reaction.
    QVERIFY(waitError([this](auto done) {
                m_service->toggleReaction(QStringLiteral("m1"), QStringLiteral("👍"), done);
            })
                .isEmpty());
    QCOMPARE(reactions(*m_worker, QStringLiteral("chat-a")).size(), 0);

    // A different emoji replaces the previous one, never adds a second row.
    QVERIFY(waitError([this](auto done) {
                m_service->toggleReaction(QStringLiteral("m1"), QStringLiteral("🔥"), done);
            })
                .isEmpty());
    QVERIFY(waitError([this](auto done) {
                m_service->toggleReaction(QStringLiteral("m1"), QStringLiteral("🎉"), done);
            })
                .isEmpty());
    const QVector<Reaction> stored = reactions(*m_worker, QStringLiteral("chat-a"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().emoji, QStringLiteral("🎉"));
    QCOMPARE(stored.first().senderLogin, QStringLiteral("me"));
}

void TestReactions::distinctLimitIsTwenty()
{
    auto seeded = m_worker->submit([](storage::Database &db) {
        storage::ReactionsRepo repo(db);
        for (int i = 0; i < kMaxDistinctReactions; ++i) {
            Reaction reaction;
            reaction.clientMsgId = QStringLiteral("m1");
            reaction.chatId = QStringLiteral("chat-a");
            reaction.senderLogin = QStringLiteral("user%1").arg(i);
            reaction.emoji = QStringLiteral("e%1").arg(i);
            reaction.createdAt = i;
            repo.set(reaction);
        }
        return repo.distinctCount(QStringLiteral("m1")).value();
    });
    seeded.waitForFinished();
    QCOMPARE(seeded.result(), kMaxDistinctReactions);

    QCOMPARE(waitError([this](auto done) {
                 m_service->toggleReaction(QStringLiteral("m1"), QStringLiteral("new"), done);
             }),
             QStringLiteral("reaction.limit"));
}

QTEST_MAIN(TestReactions)
#include "test_reactions.moc"
