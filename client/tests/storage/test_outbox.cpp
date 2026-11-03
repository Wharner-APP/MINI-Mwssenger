#include "storage/OutboxRepo.h"
#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QTemporaryDir>
#include <QtTest>

#include <optional>
#include <utility>

using namespace mini;
using namespace mini::storage;

namespace {

std::optional<OutboxItem> nextItem(StorageWorker &worker, const QString &chatId)
{
    auto future = worker.submit([chatId](Database &db) { return OutboxRepo(db).next(chatId); });
    future.waitForFinished();
    auto result = future.result();
    return result.isOk() ? result.value() : std::nullopt;
}

} // namespace

class TestOutbox : public QObject
{
    Q_OBJECT

private slots:
    void keepsPerChatOrderAcrossRestart();
    void retryLaterHidesTheTaskUntilItIsDue();
    void duplicateEnqueueIsIgnored();
};

void TestOutbox::keepsPerChatOrderAcrossRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
        auto enqueued = worker.submit([](Database &db) {
            OutboxRepo outbox(db);
            const bool a1 = outbox.enqueue(QStringLiteral("chat-a"), QStringLiteral("a1")).isOk();
            const bool a2 = outbox.enqueue(QStringLiteral("chat-a"), QStringLiteral("a2")).isOk();
            const bool a3 = outbox.enqueue(QStringLiteral("chat-a"), QStringLiteral("a3")).isOk();
            const bool b1 = outbox.enqueue(QStringLiteral("chat-b"), QStringLiteral("b1")).isOk();
            return a1 && a2 && a3 && b1;
        });
        enqueued.waitForFinished();
        QVERIFY(enqueued.result());

        auto first = nextItem(worker, QStringLiteral("chat-a"));
        QVERIFY(first.has_value());
        QCOMPARE(first->clientMsgId, QStringLiteral("a1"));

        // One task per chat: the same head is returned until it is acknowledged.
        auto again = nextItem(worker, QStringLiteral("chat-a"));
        QVERIFY(again.has_value());
        QCOMPARE(again->clientMsgId, QStringLiteral("a1"));

        auto sent = worker.submit([id = first->id](Database &db) {
            return OutboxRepo(db).markSent(id).value();
        });
        sent.waitForFinished();
        QVERIFY(sent.result());
    }

    StorageWorker reopened;
    QVERIFY2(reopened.open(path, test::dbKey()).isOk(), "reopen failed");

    auto second = nextItem(reopened, QStringLiteral("chat-a"));
    QVERIFY(second.has_value());
    QCOMPARE(second->clientMsgId, QStringLiteral("a2"));

    auto other = nextItem(reopened, QStringLiteral("chat-b"));
    QVERIFY(other.has_value());
    QCOMPARE(other->clientMsgId, QStringLiteral("b1"));
}

void TestOutbox::retryLaterHidesTheTaskUntilItIsDue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto enqueued = worker.submit([](Database &db) {
        OutboxRepo outbox(db);
        const qint64 first =
            outbox.enqueue(QStringLiteral("chat"), QStringLiteral("m1")).value();
        outbox.enqueue(QStringLiteral("chat"), QStringLiteral("m2"));
        return first;
    });
    enqueued.waitForFinished();
    const qint64 id = enqueued.result();
    QVERIFY(id > 0);

    auto delayed = worker.submit([id](Database &db) {
        return OutboxRepo(db).retryLater(id, 60000).value();
    });
    delayed.waitForFinished();
    QVERIFY(delayed.result());

    // m2 must not overtake the delayed head of the chat queue.
    QVERIFY(!nextItem(worker, QStringLiteral("chat")).has_value());

    auto due = worker.submit([id](Database &db) {
        return OutboxRepo(db).retryLater(id, 0).value();
    });
    due.waitForFinished();
    QVERIFY(due.result());

    auto item = nextItem(worker, QStringLiteral("chat"));
    QVERIFY(item.has_value());
    QCOMPARE(item->clientMsgId, QStringLiteral("m1"));
    QCOMPARE(item->attempts, 2);
}

void TestOutbox::duplicateEnqueueIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto duplicates = worker.submit([](Database &db) {
        OutboxRepo outbox(db);
        const qint64 first =
            outbox.enqueue(QStringLiteral("chat"), QStringLiteral("m1")).value();
        const qint64 second =
            outbox.enqueue(QStringLiteral("chat"), QStringLiteral("m1")).value();
        return std::make_pair(first, second);
    });
    duplicates.waitForFinished();
    QVERIFY(duplicates.result().first > 0);
    QCOMPARE(duplicates.result().second, qint64(0));
}

QTEST_GUILESS_MAIN(TestOutbox)
#include "test_outbox.moc"
