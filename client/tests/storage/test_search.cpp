#include "storage/ChatsRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace mini;
using namespace mini::storage;

namespace {

Message makeMessage(const QString &clientMsgId, const QString &body)
{
    Message message;
    message.clientMsgId = clientMsgId;
    message.chatId = QStringLiteral("chat");
    message.body = body;
    message.localTs = 100;
    return message;
}

QVector<Message> search(StorageWorker &worker, const QString &query)
{
    auto future = worker.submit([query](Database &db) {
        auto found = MessagesRepo(db).search(query);
        return found.isOk() ? found.value() : QVector<Message>();
    });
    future.waitForFinished();
    return future.result();
}

} // namespace

class TestSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void findsWordWithDiacritics();
    void findsCyrillicCaseInsensitively();
    void rebuildsIndexFromEncryptedBodies();
    void reindexesAtStartup();
    void deletedMessageLeavesTheIndex();
    void hostileQueryIsSafe();
    void cleanupTestCase();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<StorageWorker> m_worker;
};

void TestSearch::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<StorageWorker>();
    QVERIFY2(m_worker->open(m_dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto seeded = m_worker->submit([](Database &db) {
        ChatsRepo(db).upsert(Chat{QStringLiteral("chat"), QStringLiteral("direct"),
                                  QStringLiteral("alice"), QStringLiteral("Alice"), 0, {}, 0,
                                  false, false, 0});
        MessagesRepo messages(db);
        return messages.insertQueued(makeMessage(QStringLiteral("m1"),
                                                 QStringLiteral("Утренний café au lait")))
                   .value()
               && messages.insertQueued(makeMessage(QStringLiteral("m2"),
                                                    QStringLiteral("Привет, мир")))
                      .value();
    });
    seeded.waitForFinished();
    QVERIFY(seeded.result());
}

void TestSearch::findsWordWithDiacritics()
{
    const auto found = search(*m_worker, QStringLiteral("cafe"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().clientMsgId, QStringLiteral("m1"));
    QVERIFY(found.first().body.contains(QStringLiteral("café")));

    const auto accented = search(*m_worker, QStringLiteral("CAFÉ"));
    QCOMPARE(accented.size(), 1);
    QCOMPARE(accented.first().clientMsgId, QStringLiteral("m1"));
}

void TestSearch::findsCyrillicCaseInsensitively()
{
    const auto found = search(*m_worker, QStringLiteral("привет"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().clientMsgId, QStringLiteral("m2"));
}

void TestSearch::rebuildsIndexFromEncryptedBodies()
{
    auto wiped = m_worker->submit([](Database &db) {
        return db.exec(QByteArrayLiteral("DELETE FROM messages_fts")).isOk();
    });
    wiped.waitForFinished();
    QVERIFY(wiped.result());
    QCOMPARE(search(*m_worker, QStringLiteral("привет")).size(), 0);

    auto rebuilt = m_worker->submit([](Database &db) {
        auto result = MessagesRepo(db).rebuildSearchIndex();
        return result.isOk() ? result.value() : -1;
    });
    rebuilt.waitForFinished();
    QCOMPARE(rebuilt.result(), 2);
    QCOMPARE(search(*m_worker, QStringLiteral("привет")).size(), 1);
    QCOMPARE(search(*m_worker, QStringLiteral("cafe")).size(), 1);
}

void TestSearch::reindexesAtStartup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
        auto seeded = worker.submit([](Database &db) {
            ChatsRepo(db).upsert(Chat{QStringLiteral("chat"), QStringLiteral("direct"),
                                      QStringLiteral("alice"), QStringLiteral("Alice"), 0, {}, 0,
                                      false, false, 0});
            return MessagesRepo(db)
                .insertQueued(makeMessage(QStringLiteral("m1"), QStringLiteral("Привет из WAL")))
                .value();
        });
        seeded.waitForFinished();
        QVERIFY(seeded.result());
    }

    // The index is not on disk, so a fresh connection must rebuild it.
    StorageWorker reopened;
    QVERIFY2(reopened.open(path, test::dbKey()).isOk(), "reopen failed");
    const auto found = search(reopened, QStringLiteral("привет"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().clientMsgId, QStringLiteral("m1"));
}

void TestSearch::deletedMessageLeavesTheIndex()
{
    auto deleted = m_worker->submit([](Database &db) {
        return MessagesRepo(db).deleteLocal(QStringLiteral("m1")).value();
    });
    deleted.waitForFinished();
    QVERIFY(deleted.result());

    QCOMPARE(search(*m_worker, QStringLiteral("cafe")).size(), 0);
    QCOMPARE(search(*m_worker, QStringLiteral("привет")).size(), 1);
}

void TestSearch::hostileQueryIsSafe()
{
    const auto found = search(*m_worker, QStringLiteral("\"*("));
    QCOMPARE(found.size(), 0);
    QCOMPARE(search(*m_worker, QString()).size(), 0);
}

void TestSearch::cleanupTestCase()
{
    m_worker.reset();
}

QTEST_GUILESS_MAIN(TestSearch)
#include "test_search.moc"
