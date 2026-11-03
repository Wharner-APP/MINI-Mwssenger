#include "core/Clock.h"
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

template <typename T>
T waitFor(std::function<void(std::function<void(Result<T>)>)> start)
{
    bool done = false;
    T value{};
    QString error;
    start([&](Result<T> result) {
        done = true;
        if (result.isOk()) {
            value = result.value();
        } else {
            error = result.error();
        }
    });
    for (int i = 0; i < 400 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    Q_UNUSED(error);
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

} // namespace

class TestMessageActions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void editsOwnRecentText();
    void rejectsEditOutsideWindowAndForeignMessages();
    void deletesForEveryoneWithinWindow();
    void pinLimitAndUnpin();
    void forwardsIntoTargetChatWithAuthor();
    void searchesInsideOneChat();
    void archiveAndMuteUntil();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::StorageWorker> m_worker;
    std::unique_ptr<MessageService> m_service;
    std::unique_ptr<ChatService> m_chats;
    int m_counter = 0;
};

void TestMessageActions::init()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<storage::StorageWorker>();
    QVERIFY(m_worker
                ->open(m_dir.filePath(QStringLiteral("actions-%1.db").arg(++m_counter)),
                       dbKey())
                .isOk());
    seedChat(*m_worker, QStringLiteral("chat-a"), QStringLiteral("alice"), 100);
    seedChat(*m_worker, QStringLiteral("chat-b"), QStringLiteral("bob"), 100);
    m_service = std::make_unique<MessageService>(m_worker.get());
    m_service->setSelfLogin(QStringLiteral("me"));
    m_chats = std::make_unique<ChatService>(m_worker.get());
    m_chats->setSelfLogin(QStringLiteral("me"));
}

void TestMessageActions::cleanup()
{
    m_chats.reset();
    m_service.reset();
    m_worker.reset();
}

void TestMessageActions::editsOwnRecentText()
{
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                        QStringLiteral("old text"), Clock::nowMs()));
    QSignalSpy updated(m_service.get(), &MessageService::messageUpdated);

    const QString error = waitError([this](auto done) {
        m_service->editMessage(QStringLiteral("m1"), QStringLiteral("new **text**"), done);
    });
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto stored = fetchMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->body, QStringLiteral("new **text**"));
    QVERIFY(stored->editedAt > 0);
    QTRY_VERIFY_WITH_TIMEOUT(updated.count() >= 1, 2000);
}

void TestMessageActions::rejectsEditOutsideWindowAndForeignMessages()
{
    auto old = Clock::nowMs() - 49LL * 60 * 60 * 1000;
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("old"),
                        QStringLiteral("old"), old));
    QCOMPARE(waitError([this](auto done) {
                 m_service->editMessage(QStringLiteral("old"), QStringLiteral("x"), done);
             }),
             QStringLiteral("edit_window_expired"));

    auto incoming = m_worker->submit([](storage::Database &db) {
        Message message;
        message.clientMsgId = QStringLiteral("theirs");
        message.chatId = QStringLiteral("chat-a");
        message.senderLogin = QStringLiteral("alice");
        message.body = QStringLiteral("incoming");
        message.localTs = Clock::nowMs();
        message.outgoing = false;
        return storage::MessagesRepo(db).insertReceived(message).value();
    });
    incoming.waitForFinished();
    QVERIFY(incoming.result());
    QCOMPARE(waitError([this](auto done) {
                 m_service->editMessage(QStringLiteral("theirs"), QStringLiteral("x"), done);
             }),
             QStringLiteral("not_author"));
}

void TestMessageActions::deletesForEveryoneWithinWindow()
{
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                        QStringLiteral("delete me"), Clock::nowMs()));
    QSignalSpy removed(m_service.get(), &MessageService::messageRemoved);
    const QString error = waitError([this](auto done) {
        m_service->deleteForEveryone(QStringLiteral("m1"), false, done);
    });
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto deleted = fetchMessage(*m_worker, QStringLiteral("chat-a"),
                                      QStringLiteral("m1"));
    QVERIFY(deleted.has_value());
    QCOMPARE(deleted->state, MessageState::DeletedLocal);
    QVERIFY(deleted->body.isEmpty());
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(0).toString(), QStringLiteral("m1"));

    auto old = Clock::nowMs() - 49LL * 60 * 60 * 1000;
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("old"),
                        QStringLiteral("old"), old));
    QCOMPARE(waitError([this](auto done) {
                 m_service->deleteForEveryone(QStringLiteral("old"), false, done);
             }),
             QStringLiteral("delete_window_expired"));
}

void TestMessageActions::pinLimitAndUnpin()
{
    for (int i = 0; i < 21; ++i) {
        QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"),
                            QStringLiteral("p%1").arg(i),
                            QStringLiteral("message %1").arg(i), Clock::nowMs() + i));
    }
    for (int i = 0; i < 20; ++i) {
        const QString error = waitError([this, i](auto done) {
            m_service->pinMessage(QStringLiteral("chat-a"),
                                  QStringLiteral("p%1").arg(i), done);
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    QCOMPARE(waitError([this](auto done) {
                 m_service->pinMessage(QStringLiteral("chat-a"), QStringLiteral("p20"),
                                       done);
             }),
             QStringLiteral("pin_limit"));

    const QVector<Message> pinned = waitFor<QVector<Message>>([this](auto done) {
        m_service->listPinned(QStringLiteral("chat-a"), done);
    });
    QCOMPARE(pinned.size(), 20);

    const QString unpinError = waitError([this](auto done) {
        m_service->unpinMessage(QStringLiteral("chat-a"), QStringLiteral("p0"), done);
    });
    QVERIFY2(unpinError.isEmpty(), qPrintable(unpinError));
    const QVector<Message> after = waitFor<QVector<Message>>([this](auto done) {
        m_service->listPinned(QStringLiteral("chat-a"), done);
    });
    QCOMPARE(after.size(), 19);
}

void TestMessageActions::forwardsIntoTargetChatWithAuthor()
{
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                        QStringLiteral("forward me"), Clock::nowMs()));
    const int before = messageCount(*m_worker, QStringLiteral("chat-b"));
    const int queued = waitFor<int>([this](auto done) {
        m_service->forwardMessages({QStringLiteral("m1")}, {QStringLiteral("chat-b")}, done);
    });
    QCOMPARE(queued, 1);
    for (int i = 0; i < 300 && messageCount(*m_worker, QStringLiteral("chat-b")) <= before; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    QCOMPARE(messageCount(*m_worker, QStringLiteral("chat-b")), before + 1);

    auto future = m_worker->submit([](storage::Database &db) {
        return storage::MessagesRepo(db).listByChat(QStringLiteral("chat-b"), {}, 100);
    });
    future.waitForFinished();
    const QVector<Message> messages = future.result().value();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().body, QStringLiteral("forward me"));
    QCOMPARE(messages.first().forwardedFrom, QStringLiteral("me"));
    QVERIFY(messages.first().outgoing);
}

void TestMessageActions::searchesInsideOneChat()
{
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-a"), QStringLiteral("m1"),
                        QStringLiteral("needle in a"), Clock::nowMs()));
    QVERIFY(seedMessage(*m_worker, QStringLiteral("chat-b"), QStringLiteral("m2"),
                        QStringLiteral("needle in b"), Clock::nowMs()));
    const QVector<Message> found = waitFor<QVector<Message>>([this](auto done) {
        m_service->searchInChat(QStringLiteral("chat-a"), QStringLiteral("needle"), done);
    });
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().clientMsgId, QStringLiteral("m1"));
}

void TestMessageActions::archiveAndMuteUntil()
{
    bool done = false;
    m_chats->setArchived(QStringLiteral("chat-a"), true, [&](Result<bool> result) {
        done = result.isOk();
    });
    for (int i = 0; i < 300 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    QVERIFY(done);
    auto archived = fetchChat(*m_worker, QStringLiteral("chat-a"));
    QVERIFY(archived.has_value());
    QVERIFY(archived->archived);

    done = false;
    const qint64 until = Clock::nowMs() + 60LL * 60 * 1000;
    m_chats->setMutedUntil(QStringLiteral("chat-a"), until, [&](Result<bool> result) {
        done = result.isOk();
    });
    for (int i = 0; i < 300 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    auto muted = fetchChat(*m_worker, QStringLiteral("chat-a"));
    QVERIFY(muted.has_value());
    QVERIFY(muted->muted);
    QVERIFY(muted->mutedUntil >= until - 1000);

    done = false;
    m_chats->setMutedUntil(QStringLiteral("chat-a"), 0, [&](Result<bool> result) {
        done = result.isOk();
    });
    for (int i = 0; i < 300 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    auto unmuted = fetchChat(*m_worker, QStringLiteral("chat-a"));
    QVERIFY(unmuted.has_value());
    QVERIFY(!unmuted->muted);
}

QTEST_MAIN(TestMessageActions)
#include "test_message_actions.moc"
