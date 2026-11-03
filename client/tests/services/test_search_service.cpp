#include "services/SearchService.h"
#include "storage/ChatsRepo.h"
#include "storage/ContactsRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

class TestSearchService : public QObject
{
    Q_OBJECT

private slots:
    void findsChatsAndMessages();
    void returnsEmptyForNoMatchOrEmptyQuery();
    void capsEachGroupAtTwenty();
    void findsPeopleFromContacts();
};

namespace {

SearchResults runSearch(SearchService &service, const QString &query)
{
    SearchResults results;
    bool done = false;
    service.search(query, [&](Result<SearchResults> result) {
        if (result.isOk()) {
            results = result.value();
        }
        done = true;
    });
    for (int i = 0; i < 300 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return results;
}

} // namespace

void TestSearchService::findsChatsAndMessages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    seedChat(worker, QStringLiteral("alice-chat"), QStringLiteral("alice"), 100);
    seedChat(worker, QStringLiteral("bob-chat"), QStringLiteral("bob"), 50);
    seedMessage(worker, QStringLiteral("alice-chat"), QStringLiteral("m1"),
                QStringLiteral("Привет, мир"), 100);
    seedMessage(worker, QStringLiteral("alice-chat"), QStringLiteral("m2"),
                QStringLiteral("утренний кофе"), 200);

    SearchService service(&worker);

    const SearchResults byChat = runSearch(service, QStringLiteral("ali"));
    QCOMPARE(byChat.chats.size(), 1);
    QCOMPARE(byChat.chats.first().chatId, QStringLiteral("alice-chat"));

    const SearchResults byMessage = runSearch(service, QStringLiteral("привет"));
    QCOMPARE(byMessage.messages.size(), 1);
    QCOMPARE(byMessage.messages.first().clientMsgId, QStringLiteral("m1"));
    QCOMPARE(byMessage.messages.first().chatId, QStringLiteral("alice-chat"));
}

void TestSearchService::returnsEmptyForNoMatchOrEmptyQuery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    seedChat(worker, QStringLiteral("chat"), QStringLiteral("alice"));
    seedMessage(worker, QStringLiteral("chat"), QStringLiteral("m1"),
                QStringLiteral("hello"), 100);

    SearchService service(&worker);
    const SearchResults none = runSearch(service, QStringLiteral("zzz-nothing"));
    QVERIFY(none.chats.isEmpty());
    QVERIFY(none.messages.isEmpty());

    const SearchResults empty = runSearch(service, QStringLiteral("   "));
    QVERIFY(empty.chats.isEmpty());
    QVERIFY(empty.messages.isEmpty());
    QVERIFY(empty.people.isEmpty());
}

void TestSearchService::capsEachGroupAtTwenty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    for (int i = 0; i < 25; ++i) {
        const QString chatId = QStringLiteral("chat-%1").arg(i);
        seedChat(worker, chatId, QStringLiteral("peer-%1").arg(i));
        seedMessage(worker, chatId, QStringLiteral("message-%1").arg(i),
                    QStringLiteral("bulk token %1").arg(i), 100 + i);
    }

    SearchService service(&worker);
    const SearchResults bulk = runSearch(service, QStringLiteral("bulk"));
    QCOMPARE(bulk.messages.size(), SearchService::kMaxPerGroup);

    const SearchResults chats = runSearch(service, QStringLiteral("peer-"));
    QCOMPARE(chats.chats.size(), SearchService::kMaxPerGroup);
}

void TestSearchService::findsPeopleFromContacts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    auto seeded = worker.submit([](storage::Database &db) {
        Contact contact;
        contact.login = QStringLiteral("dave");
        contact.displayName = QStringLiteral("Dave Grohl");
        return storage::ContactsRepo(db).upsert(contact).isOk();
    });
    seeded.waitForFinished();
    QVERIFY(seeded.result());

    SearchService service(&worker);
    const SearchResults people = runSearch(service, QStringLiteral("dav"));
    QCOMPARE(people.people.size(), 1);
    QCOMPARE(people.people.first().login, QStringLiteral("dave"));
}

QTEST_MAIN(TestSearchService)
#include "test_search_service.moc"
