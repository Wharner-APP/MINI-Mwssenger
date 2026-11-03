#include "core/Clock.h"
#include "domain/Disappearing.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/ChatsRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

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

bool seedIncoming(storage::StorageWorker &worker, const QString &chatId,
                  const QString &clientMsgId, qint64 ts)
{
    auto future = worker.submit([=](storage::Database &db) {
        Message message;
        message.clientMsgId = clientMsgId;
        message.chatId = chatId;
        message.senderLogin = QStringLiteral("alice");
        message.body = QStringLiteral("secret");
        message.localTs = ts;
        message.serverTs = ts;
        message.outgoing = false;
        return storage::MessagesRepo(db).insertReceived(message).value();
    });
    future.waitForFinished();
    return future.result();
}

} // namespace

class TestExpiry : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void timerValuesAndValidation();
    void readStartsCountdown();
    void cleanerDeletesExpiredAndEmits();
    void readReceiptExpiryForOutgoing();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::StorageWorker> m_worker;
    std::unique_ptr<MessageService> m_messages;
    std::unique_ptr<ChatService> m_chats;
    int m_counter = 0;
};

void TestExpiry::init()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<storage::StorageWorker>();
    QVERIFY(m_worker
                ->open(m_dir.filePath(QStringLiteral("expiry-%1.db").arg(++m_counter)),
                       dbKey())
                .isOk());
    seedChat(*m_worker, QStringLiteral("chat-a"), QStringLiteral("alice"), 100);
    m_messages = std::make_unique<MessageService>(m_worker.get());
    m_messages->setSelfLogin(QStringLiteral("me"));
    m_chats = std::make_unique<ChatService>(m_worker.get());
    m_chats->setSelfLogin(QStringLiteral("me"));
}

void TestExpiry::cleanup()
{
    m_chats.reset();
    m_messages.reset();
    m_worker.reset();
}

void TestExpiry::timerValuesAndValidation()
{
    QCOMPARE(disappearing::values().size(), 6);
    QVERIFY(disappearing::isValid(0));
    QVERIFY(disappearing::isValid(disappearing::kMinute));
    QVERIFY(disappearing::isValid(disappearing::kWeek));
    QVERIFY(!disappearing::isValid(12345));
    QVERIFY(!disappearing::isValid(-1));

    QCOMPARE(waitError([this](auto done) {
                 m_messages->setChatExpiry(QStringLiteral("chat-a"), 12345, done);
             }),
             QStringLiteral("expiry.invalid"));
}

void TestExpiry::readStartsCountdown()
{
    QVERIFY(waitError([this](auto done) {
                m_messages->setChatExpiry(QStringLiteral("chat-a"),
                                          disappearing::kMinute, done);
            })
                .isEmpty());

    auto chat = m_worker->submit([](storage::Database &db) {
        return storage::ChatsRepo(db).get(QStringLiteral("chat-a")).value();
    });
    chat.waitForFinished();
    QVERIFY(chat.result().has_value());
    QCOMPARE(chat.result()->expiresAfterMs, disappearing::kMinute);

    QVERIFY(seedIncoming(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                         Clock::nowMs() - 5000));

    bool read = false;
    m_chats->markRead(QStringLiteral("chat-a"),
                      [&read](Result<int> result) { read = result.isOk(); });
    for (int i = 0; i < 200 && !read; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    QVERIFY(read);

    const auto stored = fetchMessage(*m_worker, QStringLiteral("chat-a"),
                                     QStringLiteral("m1"));
    QVERIFY(stored.has_value());
    QVERIFY(stored->expiresAt > 0);
    QVERIFY(stored->expiresAt >= Clock::nowMs() + disappearing::kMinute - 5000);
}

void TestExpiry::cleanerDeletesExpiredAndEmits()
{
    QVERIFY(seedIncoming(*m_worker, QStringLiteral("chat-a"), QStringLiteral("old"),
                         Clock::nowMs() - 10000));
    QVERIFY(seedIncoming(*m_worker, QStringLiteral("chat-a"), QStringLiteral("keep"),
                         Clock::nowMs() - 9000));
    auto forced = m_worker->submit([](storage::Database &db) {
        return db.exec(QByteArrayLiteral(
                           "UPDATE messages SET expires_at = 1 WHERE client_msg_id = 'old'"))
            .isOk();
    });
    forced.waitForFinished();
    QVERIFY(forced.result());

    QSignalSpy removed(m_messages.get(), &MessageService::messageRemoved);
    m_messages->runExpiryCleanup();
    QTRY_VERIFY_WITH_TIMEOUT(removed.count() >= 1, 3000);

    QCOMPARE(messageCount(*m_worker, QStringLiteral("chat-a")), 1);
    QVERIFY(fetchMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("keep"))
                .has_value());
}

void TestExpiry::readReceiptExpiryForOutgoing()
{
    // §39.2: our own message starts its countdown when the peer reads it.
    // The repository method is the exact step the WS receipt performs.
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("mine"),
                        QStringLiteral("outgoing"), Clock::nowMs()));
    auto applied = m_worker->submit([](storage::Database &db) {
        return storage::MessagesRepo(db).applyReadExpiry(
            {QStringLiteral("mine")}, disappearing::kHour, Clock::nowMs());
    });
    applied.waitForFinished();
    QVERIFY(applied.result().isOk());
    QCOMPARE(applied.result().value(), 1);

    const auto stored = fetchMessage(*m_worker, QStringLiteral("chat-a"),
                                     QStringLiteral("mine"));
    QVERIFY(stored.has_value());
    QVERIFY(stored->expiresAt > 0);
}

QTEST_MAIN(TestExpiry)
#include "test_expiry.moc"
