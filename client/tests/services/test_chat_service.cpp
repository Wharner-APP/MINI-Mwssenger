#include "services/ChatService.h"
#include "storage/ChatsRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

class TestChatService : public QObject
{
    Q_OBJECT

private slots:
    void opensDirectChatByExactLogin();
    void rejectsInvalidPeerLogin();
    void listsChatsAndPersistsPinAndMute();
    void deletesChatWithAndWithoutHistory();
    void draftSurvivesRestartAndStaysEncrypted();
    void marksChatRead();
};

void TestChatService::opensDirectChatByExactLogin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ChatService service(&worker);
    service.setSelfLogin(QStringLiteral("me"));

    Chat first;
    bool done = false;
    service.openDirectChat(QStringLiteral("alice"), [&](Result<Chat> result) {
        QVERIFY(result.isOk());
        first = result.value();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);

    QCOMPARE(first.chatId, ChatService::directChatId(QStringLiteral("me"),
                                                     QStringLiteral("alice")));
    QCOMPARE(first.peerLogin, QStringLiteral("alice"));
    QCOMPARE(first.title, QStringLiteral("alice"));
    QCOMPARE(first.kind, QStringLiteral("direct"));

    Chat again;
    bool second = false;
    service.openDirectChat(QStringLiteral("Alice"), [&](Result<Chat> result) {
        again = result.isOk() ? result.value() : Chat{};
        second = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(second, 3000);
    QCOMPARE(again.chatId, first.chatId);
    QCOMPARE(chatCount(worker), 1);

    Chat other;
    bool third = false;
    service.openDirectChat(QStringLiteral("bob_1"), [&](Result<Chat> result) {
        other = result.isOk() ? result.value() : Chat{};
        third = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(third, 3000);
    QVERIFY(other.chatId != first.chatId);
    QCOMPARE(chatCount(worker), 2);
}

void TestChatService::rejectsInvalidPeerLogin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ChatService service(&worker);
    service.setSelfLogin(QStringLiteral("me"));

    QString error;
    bool done = false;
    service.openDirectChat(QStringLiteral("a"), [&](Result<Chat> result) {
        error = result.error();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(error, QStringLiteral("login.invalid_format"));
    QCOMPARE(chatCount(worker), 0);

    bool formatOk = ChatService::validatePeerLogin(QStringLiteral("sergey_77")).isOk();
    QVERIFY(formatOk);
    QVERIFY(ChatService::validatePeerLogin(QStringLiteral("admin")).isError());
    QVERIFY(ChatService::validatePeerLogin(QStringLiteral("bot_helper")).isError());
}

void TestChatService::listsChatsAndPersistsPinAndMute()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    seedChat(worker, QStringLiteral("a"), QStringLiteral("alice"), 200);
    seedChat(worker, QStringLiteral("b"), QStringLiteral("bob"), 100);

    ChatService service(&worker);
    QVector<Chat> chats;
    bool listed = false;
    service.listChats([&](Result<QVector<Chat>> result) {
        chats = result.isOk() ? result.value() : QVector<Chat>();
        listed = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(listed, 3000);
    QCOMPARE(chats.size(), 2);
    QCOMPARE(chats.at(0).chatId, QStringLiteral("a"));
    QCOMPARE(chats.at(1).chatId, QStringLiteral("b"));

    bool pinned = false;
    service.setPinned(QStringLiteral("b"), true, [&](Result<bool> result) {
        pinned = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(pinned, 3000);

    bool muted = false;
    service.setMuted(QStringLiteral("a"), true, [&](Result<bool> result) {
        muted = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(muted, 3000);

    QVERIFY(fetchChat(worker, QStringLiteral("b"))->pinned);
    QVERIFY(fetchChat(worker, QStringLiteral("a"))->muted);

    listed = false;
    service.listChats([&](Result<QVector<Chat>> result) {
        chats = result.isOk() ? result.value() : QVector<Chat>();
        listed = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(listed, 3000);
    QCOMPARE(chats.at(0).chatId, QStringLiteral("b"));
}

void TestChatService::deletesChatWithAndWithoutHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    seedChat(worker, QStringLiteral("keep-history"), QStringLiteral("alice"));
    seedMessage(worker, QStringLiteral("keep-history"), QStringLiteral("m1"),
                QStringLiteral("one"), 100);
    seedMessage(worker, QStringLiteral("keep-history"), QStringLiteral("m2"),
                QStringLiteral("two"), 200);

    seedChat(worker, QStringLiteral("wipe-history"), QStringLiteral("bob"));
    seedMessage(worker, QStringLiteral("wipe-history"), QStringLiteral("m3"),
                QStringLiteral("three"), 100);

    ChatService service(&worker);

    bool removed = false;
    service.deleteChat(QStringLiteral("keep-history"), false, [&](Result<bool> result) {
        removed = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(removed, 3000);
    QVERIFY(!fetchChat(worker, QStringLiteral("keep-history")).has_value());
    QCOMPARE(messageCount(worker, QStringLiteral("keep-history")), 2);

    removed = false;
    service.deleteChat(QStringLiteral("wipe-history"), true, [&](Result<bool> result) {
        removed = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(removed, 3000);
    QVERIFY(!fetchChat(worker, QStringLiteral("wipe-history")).has_value());
    QCOMPARE(messageCount(worker, QStringLiteral("wipe-history")), 0);
}

void TestChatService::draftSurvivesRestartAndStaysEncrypted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("db.sqlite"));
    const QString marker = QStringLiteral("DRAFT-MARKER-7f3a-невидимый");

    {
        storage::StorageWorker worker;
        QVERIFY(worker.open(path, dbKey()).isOk());
        seedChat(worker, QStringLiteral("chat"), QStringLiteral("alice"));

        ChatService service(&worker);
        bool saved = false;
        service.saveDraft(QStringLiteral("chat"), marker, [&](Result<bool> result) {
            saved = result.isOk() && result.value();
        });
        QTRY_VERIFY_WITH_TIMEOUT(saved, 3000);

        QString loaded;
        bool read = false;
        service.loadDraft(QStringLiteral("chat"), [&](Result<QString> result) {
            loaded = result.isOk() ? result.value() : QString();
            read = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(read, 3000);
        QCOMPARE(loaded, marker);
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    QVERIFY(!bytes.contains(marker.toUtf8()));

    storage::StorageWorker reopened;
    QVERIFY(reopened.open(path, dbKey()).isOk());
    ChatService service(&reopened);
    QString loaded;
    bool read = false;
    service.loadDraft(QStringLiteral("chat"), [&](Result<QString> result) {
        loaded = result.isOk() ? result.value() : QString();
        read = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(read, 3000);
    QCOMPARE(loaded, marker);

    bool cleared = false;
    service.clearDraft(QStringLiteral("chat"), [&](Result<bool> result) {
        cleared = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(cleared, 3000);

    read = false;
    service.loadDraft(QStringLiteral("chat"), [&](Result<QString> result) {
        loaded = result.isOk() ? result.value() : QStringLiteral("kept");
        read = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(read, 3000);
    QVERIFY(loaded.isEmpty());
}

void TestChatService::marksChatRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    seedChat(worker, QStringLiteral("chat"), QStringLiteral("alice"), 100);
    auto seeded = worker.submit([](storage::Database &db) {
        Message incoming;
        incoming.clientMsgId = QStringLiteral("m1");
        incoming.chatId = QStringLiteral("chat");
        incoming.body = QStringLiteral("hello");
        incoming.localTs = 100;
        incoming.outgoing = false;
        const bool inserted = storage::MessagesRepo(db).insertReceived(incoming).value();
        storage::ChatsRepo(db).touchLastMessage(QStringLiteral("chat"), 100,
                                                QStringLiteral("hello"), true);
        return inserted;
    });
    seeded.waitForFinished();
    QVERIFY(seeded.result());
    QCOMPARE(fetchChat(worker, QStringLiteral("chat"))->unreadCount, 1);

    ChatService service(&worker);
    int count = -1;
    bool done = false;
    service.markRead(QStringLiteral("chat"), [&](Result<int> result) {
        count = result.isOk() ? result.value() : -1;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(count, 1);
    QCOMPARE(fetchChat(worker, QStringLiteral("chat"))->unreadCount, 0);
}

QTEST_MAIN(TestChatService)
#include "test_chat_service.moc"
