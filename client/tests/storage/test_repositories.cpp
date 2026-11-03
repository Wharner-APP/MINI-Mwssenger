#include "storage/ChatsRepo.h"
#include "storage/ContactsRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/OutboxRepo.h"
#include "storage/SettingsRepo.h"
#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <tuple>
#include <utility>

using namespace mini;
using namespace mini::storage;

namespace {

Message makeMessage(const QString &clientMsgId, const QString &chatId, const QString &body,
                    qint64 localTs, bool outgoing = true)
{
    Message message;
    message.clientMsgId = clientMsgId;
    message.chatId = chatId;
    message.body = body;
    message.localTs = localTs;
    message.outgoing = outgoing;
    return message;
}

Chat makeChat(const QString &chatId, const QString &title, qint64 lastTs)
{
    Chat chat;
    chat.chatId = chatId;
    chat.title = title;
    chat.peerLogin = title.toLower();
    chat.lastMessageTs = lastTs;
    return chat;
}

} // namespace

class TestRepositories : public QObject
{
    Q_OBJECT

private slots:
    void chatsAndSettingsAndContacts();
    void messageOrderAndDedupe();
    void bodyIsNotPlaintextOnDisk();
    void markReadAndDeleteLocal();
    void removingChatDropsItsOutboxRows();
};

void TestRepositories::chatsAndSettingsAndContacts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto stored = worker.submit([](Database &db) {
        ChatsRepo chats(db);
        const bool a = chats.upsert(makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 100))
                           .isOk();
        const bool b = chats.upsert(makeChat(QStringLiteral("b"), QStringLiteral("Bob"), 300))
                           .isOk();
        const bool c = chats.upsert(makeChat(QStringLiteral("c"), QStringLiteral("Carol"), 200))
                           .isOk();
        const bool pinned = chats.setPinned(QStringLiteral("c"), true).isOk();
        const bool muted = chats.setMuted(QStringLiteral("a"), true).isOk();

        SettingsRepo settings(db);
        const bool set = settings.set(QStringLiteral("ui/theme"), QStringLiteral("amoled")).isOk();

        ContactsRepo contacts(db);
        Contact contact;
        contact.login = QStringLiteral("dave");
        contact.displayName = QStringLiteral("Dave");
        return a && b && c && pinned && muted && set && contacts.upsert(contact).isOk();
    });
    stored.waitForFinished();
    QVERIFY(stored.result());

    auto listed = worker.submit([](Database &db) {
        auto chats = ChatsRepo(db).list();
        auto theme = SettingsRepo(db).get(QStringLiteral("ui/theme"), QString());
        auto contacts = ContactsRepo(db).list();
        return std::make_tuple(chats.isOk() ? chats.value() : QVector<Chat>(),
                               theme.isOk() ? theme.value() : QString(),
                               contacts.isOk() ? contacts.value() : QVector<Contact>());
    });
    listed.waitForFinished();

    const auto chats = std::get<0>(listed.result());
    QCOMPARE(chats.size(), 3);
    QCOMPARE(chats.at(0).chatId, QStringLiteral("c")); // pinned first
    QCOMPARE(chats.at(1).chatId, QStringLiteral("b")); // 300 > 100
    QCOMPARE(chats.at(2).chatId, QStringLiteral("a"));
    QVERIFY(chats.at(2).muted);

    QCOMPARE(std::get<1>(listed.result()), QStringLiteral("amoled"));
    QCOMPARE(std::get<2>(listed.result()).size(), 1);
    QCOMPARE(std::get<2>(listed.result()).first().displayName, QStringLiteral("Dave"));
}

void TestRepositories::messageOrderAndDedupe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto seeded = worker.submit([](Database &db) {
        ChatsRepo(db).upsert(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0));
        MessagesRepo messages(db);
        const bool first = messages.insertQueued(
            makeMessage(QStringLiteral("m1"), QStringLiteral("chat"), QStringLiteral("one"), 100))
                               .value();
        const bool second = messages.insertQueued(
            makeMessage(QStringLiteral("m2"), QStringLiteral("chat"), QStringLiteral("two"), 200))
                                .value();
        const bool third = messages.insertQueued(
            makeMessage(QStringLiteral("m3"), QStringLiteral("chat"), QStringLiteral("three"), 300))
                               .value();
        const bool duplicate = messages.insertQueued(
            makeMessage(QStringLiteral("m1"), QStringLiteral("chat"), QStringLiteral("one"), 100))
                                   .value();
        return first && second && third && !duplicate;
    });
    seeded.waitForFinished();
    QVERIFY2(seeded.result(), "seed failed");

    auto beforeConfirmation = worker.submit([](Database &db) {
        auto messages = MessagesRepo(db).listByChat(QStringLiteral("chat"));
        return messages.isOk() ? messages.value() : QVector<Message>();
    });
    beforeConfirmation.waitForFinished();
    const auto pending = beforeConfirmation.result();
    QCOMPARE(pending.size(), 3);
    QCOMPARE(pending.at(0).clientMsgId, QStringLiteral("m1"));
    QCOMPARE(pending.at(1).clientMsgId, QStringLiteral("m2"));
    QCOMPARE(pending.at(2).clientMsgId, QStringLiteral("m3"));

    // §4.8: once confirmed, ordering follows server_ts, not local_ts.
    auto confirmed = worker.submit([](Database &db) {
        MessagesRepo(db).updateState(QStringLiteral("m1"), MessageState::Sent, 9000,
                                     QStringLiteral("srv-1"));
        auto messages = MessagesRepo(db).listByChat(QStringLiteral("chat"));
        return messages.isOk() ? messages.value() : QVector<Message>();
    });
    confirmed.waitForFinished();
    const auto reordered = confirmed.result();
    QCOMPARE(reordered.size(), 3);
    QCOMPARE(reordered.at(0).clientMsgId, QStringLiteral("m2"));
    QCOMPARE(reordered.at(1).clientMsgId, QStringLiteral("m3"));
    QCOMPARE(reordered.at(2).clientMsgId, QStringLiteral("m1"));
    QCOMPARE(reordered.at(2).serverMsgId, QStringLiteral("srv-1"));

    // Cursor paging walks backwards without gaps or repeats.
    auto page = worker.submit([](Database &db) {
        MessagesRepo messages(db);
        auto latest = messages.listByChat(QStringLiteral("chat"), {}, 2);
        if (latest.isError() || latest.value().size() != 2) {
            return QVector<Message>();
        }
        const Message first = latest.value().first();
        MessageCursor cursor;
        cursor.ts = first.serverTs > 0 ? first.serverTs : first.localTs;
        cursor.clientMsgId = first.clientMsgId;
        auto older = messages.listByChat(QStringLiteral("chat"), cursor, 2);
        return older.isOk() ? older.value() : QVector<Message>();
    });
    page.waitForFinished();
    QCOMPARE(page.result().size(), 1);
    QCOMPARE(page.result().first().clientMsgId, QStringLiteral("m2"));
}

void TestRepositories::bodyIsNotPlaintextOnDisk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString marker = QStringLiteral("СЕКРЕТНЫЙ-МАРКЕР-7f3a");
    const QString previewMarker = QStringLiteral("ПРЕВЬЮ-МАРКЕР-91b2");
    // Lowercase, no punctuation: exactly what an on-disk FTS index would keep.
    const QString tokenMarker = QStringLiteral("невидимоеслово42");

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
        auto seeded = worker.submit([&marker, &previewMarker, &tokenMarker](Database &db) {
            ChatsRepo(db).upsert(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0));
            ChatsRepo(db).touchLastMessage(QStringLiteral("chat"), 100, previewMarker, false);
            return MessagesRepo(db)
                .insertQueued(makeMessage(QStringLiteral("m1"), QStringLiteral("chat"),
                                          QStringLiteral("тело ") + marker + QLatin1Char(' ')
                                              + tokenMarker,
                                          100))
                .value();
        });
        seeded.waitForFinished();
        QVERIFY(seeded.result());

        auto readBack = worker.submit([](Database &db) {
            auto messages = MessagesRepo(db).listByChat(QStringLiteral("chat"));
            auto chats = ChatsRepo(db).list();
            return std::make_pair(messages.isOk() ? messages.value() : QVector<Message>(),
                                  chats.isOk() ? chats.value() : QVector<Chat>());
        });
        readBack.waitForFinished();
        QCOMPARE(readBack.result().first.size(), 1);
        QVERIFY(readBack.result().first.first().body.contains(marker));
        QCOMPARE(readBack.result().second.first().lastMessagePreview, previewMarker);
    }

    QFile database(path);
    QVERIFY(database.open(QIODevice::ReadOnly));
    const QByteArray bytes = database.readAll();
    database.close();
    QVERIFY(!bytes.contains(marker.toUtf8()));
    QVERIFY(!bytes.contains(previewMarker.toUtf8()));
    QVERIFY(!bytes.contains(tokenMarker.toUtf8()));

    QFile wal(path + QStringLiteral("-wal"));
    if (wal.exists()) {
        QVERIFY(wal.open(QIODevice::ReadOnly));
        const QByteArray walBytes = wal.readAll();
        QVERIFY(!walBytes.contains(marker.toUtf8()));
        QVERIFY(!walBytes.contains(previewMarker.toUtf8()));
        QVERIFY(!walBytes.contains(tokenMarker.toUtf8()));
        wal.close();
    }

    // The same DB_KEY decrypts the body after a restart.
    StorageWorker reopened;
    QVERIFY2(reopened.open(path, test::dbKey()).isOk(), "reopen failed");
    auto readBack = reopened.submit([](Database &db) {
        auto messages = MessagesRepo(db).listByChat(QStringLiteral("chat"));
        return messages.isOk() ? messages.value() : QVector<Message>();
    });
    readBack.waitForFinished();
    QCOMPARE(readBack.result().size(), 1);
    QVERIFY(readBack.result().first().body.contains(marker));
}

void TestRepositories::markReadAndDeleteLocal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto seeded = worker.submit([](Database &db) {
        ChatsRepo(db).upsert(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0));
        MessagesRepo messages(db);
        Message incoming =
            makeMessage(QStringLiteral("m1"), QStringLiteral("chat"), QStringLiteral("hi"), 100);
        incoming.outgoing = false;
        incoming.state = MessageState::Received;
        const bool inserted = messages.insertReceived(incoming).value();
        ChatsRepo(db).touchLastMessage(QStringLiteral("chat"), 100, QStringLiteral("hi"), true);
        return inserted;
    });
    seeded.waitForFinished();
    QVERIFY2(seeded.result(), "seed failed");

    auto result = worker.submit([](Database &db) {
        MessagesRepo messages(db);
        const int read = messages.markRead(QStringLiteral("chat")).value();
        const bool removed = messages.deleteLocal(QStringLiteral("m1")).value();
        auto remaining = messages.listByChat(QStringLiteral("chat"));
        auto chats = ChatsRepo(db).list();
        return std::make_tuple(read, removed,
                               remaining.isOk() ? remaining.value() : QVector<Message>(),
                               chats.isOk() ? chats.value() : QVector<Chat>());
    });
    result.waitForFinished();

    QCOMPARE(std::get<0>(result.result()), 1);
    QVERIFY(std::get<1>(result.result()));
    const auto messages = std::get<2>(result.result());
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().state, MessageState::DeletedLocal);
    QVERIFY(messages.first().body.isEmpty());
    QCOMPARE(std::get<3>(result.result()).first().unreadCount, 0);
}

void TestRepositories::removingChatDropsItsOutboxRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    auto result = worker.submit([](Database &db) {
        ChatsRepo chats(db);
        chats.upsert(makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 100));
        chats.upsert(makeChat(QStringLiteral("b"), QStringLiteral("Bob"), 100));
        OutboxRepo outbox(db);
        outbox.enqueue(QStringLiteral("a"), QStringLiteral("m-a"), 100);
        outbox.enqueue(QStringLiteral("b"), QStringLiteral("m-b"), 100);

        const bool removed = chats.remove(QStringLiteral("a")).value();
        const auto nextA = outbox.next(QStringLiteral("a"));
        const auto nextB = outbox.next(QStringLiteral("b"));
        return std::make_tuple(removed, nextA.isOk() && nextA.value().has_value(),
                               nextB.isOk() && nextB.value().has_value());
    });
    result.waitForFinished();

    QVERIFY(std::get<0>(result.result()));
    QVERIFY(!std::get<1>(result.result()));
    QVERIFY(std::get<2>(result.result()));
}

QTEST_GUILESS_MAIN(TestRepositories)
#include "test_repositories.moc"
