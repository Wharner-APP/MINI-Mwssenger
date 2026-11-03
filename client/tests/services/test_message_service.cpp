#include "core/Clock.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/ChatsRepo.h"
#include "storage/OutboxRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

class FakeCryptoSession : public CryptoSession
{
public:
    Result<QByteArray> encrypt(const QString &chatId, const QByteArray &plaintext,
                               QJsonObject *header) override
    {
        Q_UNUSED(chatId);
        if (header) {
            header->insert(QStringLiteral("v"), 1);
        }
        QByteArray sealed = plaintext;
        for (char &byte : sealed) {
            byte = static_cast<char>(byte ^ 0x5a);
        }
        return Result<QByteArray>::ok(sealed);
    }

    Result<QByteArray> decrypt(const QString &chatId, const QByteArray &ciphertext,
                               const QJsonObject &header) override
    {
        Q_UNUSED(chatId);
        Q_UNUSED(header);
        QByteArray plain = ciphertext;
        for (char &byte : plain) {
            byte = static_cast<char>(byte ^ 0x5a);
        }
        return Result<QByteArray>::ok(plain);
    }
};

class FakeTransport : public MessageTransport
{
public:
    enum class Mode { Accept, Reject, NetworkFailure, Manual };

    explicit FakeTransport(QObject *parent = nullptr) : MessageTransport(parent) {}

    void send(const QList<net::Envelope> &envelopes, SendCallback done) override
    {
        if (onSend) {
            onSend();
        }
        for (const net::Envelope &envelope : envelopes) {
            sent.append(envelope);
        }
        if (mode == Mode::Manual) {
            m_pending.append(done);
            return;
        }
        if (mode == Mode::NetworkFailure) {
            done(net::ApiResult<net::SendResult>::fail(net::ApiError::network("offline")));
            return;
        }
        net::SendResult result;
        for (const net::Envelope &envelope : envelopes) {
            if (mode == Mode::Accept) {
                result.accepted.append(envelope.clientMsgId);
            } else {
                result.rejected.append(envelope.clientMsgId);
            }
        }
        done(net::ApiResult<net::SendResult>::ok(result));
    }

    [[nodiscard]] bool isOnline() const override { return online; }

    void completeNext(bool accept)
    {
        if (m_pending.isEmpty()) {
            return;
        }
        SendCallback callback = m_pending.takeFirst();
        net::SendResult result;
        const net::Envelope &envelope = sent.last();
        if (accept) {
            result.accepted.append(envelope.clientMsgId);
        } else {
            result.rejected.append(envelope.clientMsgId);
        }
        callback(net::ApiResult<net::SendResult>::ok(result));
    }

    void setOnline(bool value)
    {
        online = value;
        emit onlineChanged(value);
    }

    void deliverIncoming(const net::Envelope &envelope) { emit messageReceived(envelope); }
    void deliverAck(const QString &clientMsgId, const QString &chatId)
    {
        emit acknowledged(clientMsgId, chatId);
    }
    void deliverStatus(const QString &clientMsgId, const QString &chatId, qint64 at)
    {
        emit delivered(clientMsgId, chatId, at);
    }
    void deliverRead(const QString &chatId, const QStringList &ids)
    {
        emit readReceipt(chatId, ids);
    }

    Mode mode = Mode::Accept;
    bool online = true;
    QList<net::Envelope> sent;
    std::function<void()> onSend;

private:
    QList<SendCallback> m_pending;
};

struct Fixture
{
    QTemporaryDir dir;
    std::unique_ptr<storage::StorageWorker> worker;
    FakeCryptoSession crypto;
    std::unique_ptr<FakeTransport> transport;
    std::unique_ptr<MessageService> service;
    QString selfLogin = QStringLiteral("matvey");
    QString chatId = ChatService::directChatId(QStringLiteral("matvey"),
                                               QStringLiteral("alice"));

    bool open()
    {
        if (!dir.isValid()) {
            return false;
        }
        worker = std::make_unique<storage::StorageWorker>();
        if (worker->open(dir.filePath(QStringLiteral("messages.db")), dbKey()).isError()) {
            return false;
        }
        transport = std::make_unique<FakeTransport>();
        service = std::make_unique<MessageService>(worker.get());
        service->setSelfLogin(selfLogin);
        service->setCryptoSession(&crypto);
        service->setTransport(transport.get());
        service->setCancelWindowMs(0);
        return seedChat(*worker, chatId, QStringLiteral("alice"));
    }
};

} // namespace

class TestMessageService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<Message>("mini::Message");
    }

    void writesDbBeforeNetworkAndReachesSent();
    void offlineQueueWaitsAndFlushesOnReconnect();
    void repeatedClientMsgIdCreatesOneMessage();
    void cancelWorksInsideWindowAndRefusesAfterSend();
    void cancelWindowHoldsTheMessage();
    void deliveryAndReadUpdateStatusWithoutDowngrade();
    void receivesMessageIncrementsUnreadAndMarksRead();
    void duplicateIncomingIsIgnored();
    void incomingCreatesMissingChat();
    void rejectsSpoofedChatId();
    void keepsPerChatOrder();
    void retriesAfterNetworkFailure();
    void pendingOutboxSurvivesRestart();
};

void TestMessageService::writesDbBeforeNetworkAndReachesSent()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    bool presentWhenSent = false;
    fixture.transport->onSend = [&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
        presentWhenSent = stored.has_value();
    };

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    const QString id = fixture.service->sendText(fixture.chatId, QStringLiteral("hello"),
                                                  {}, QStringLiteral("m1"));
    QCOMPARE(id, QStringLiteral("m1"));

    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 3000);
    QVERIFY2(presentWhenSent, "the row must exist before the envelope leaves");

    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, id);
        return stored && stored->state == MessageState::Sent;
    }(), 3000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->body, QStringLiteral("hello"));
    QVERIFY(stored->outgoing);
    QCOMPARE(stored->clientMsgId, id);

    const net::Envelope &envelope = fixture.transport->sent.first();
    QCOMPARE(envelope.clientMsgId, id);
    QCOMPARE(envelope.chatId, fixture.chatId);
    QVERIFY(!envelope.ciphertext.contains("hello"));
}

void TestMessageService::offlineQueueWaitsAndFlushesOnReconnect()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.transport->online = false;

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendText(fixture.chatId, QStringLiteral("later"), {}, QStringLiteral("m1"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);

    fixture.service->flushQueue();
    QTest::qWait(150);
    QCOMPARE(fixture.transport->sent.size(), 0);
    const auto queued = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
    QVERIFY(queued.has_value());
    QCOMPARE(queued->state, MessageState::Queued);

    fixture.transport->setOnline(true);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
        return stored && stored->state == MessageState::Sent;
    }(), 3000);
}

void TestMessageService::repeatedClientMsgIdCreatesOneMessage()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendText(fixture.chatId, QStringLiteral("once"), {},
                              QStringLiteral("same-id"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);

    const QString second = fixture.service->sendText(fixture.chatId, QStringLiteral("once"), {},
                                                     QStringLiteral("same-id"));
    QCOMPARE(second, QStringLiteral("same-id"));
    QTest::qWait(200);

    QCOMPARE(added.count(), 1);
    QCOMPARE(messageCount(*fixture.worker, fixture.chatId), 1);
    QCOMPARE(fixture.transport->sent.size(), 1);
}

void TestMessageService::cancelWorksInsideWindowAndRefusesAfterSend()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.service->setCancelWindowMs(5000);

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendText(fixture.chatId, QStringLiteral("oops"), {},
                              QStringLiteral("cancel-me"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);

    bool cancelled = false;
    fixture.service->cancelSend(QStringLiteral("cancel-me"), [&](Result<bool> result) {
        cancelled = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(cancelled, 3000);

    const auto removed = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("cancel-me"));
    QVERIFY(removed.has_value());
    QCOMPARE(removed->state, MessageState::DeletedLocal);
    QVERIFY(removed->body.isEmpty());
    QCOMPARE(fixture.transport->sent.size(), 0);

    // A message that already left cannot be cancelled.
    fixture.service->setCancelWindowMs(0);
    fixture.service->sendText(fixture.chatId, QStringLiteral("gone"), {},
                              QStringLiteral("already-sent"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored =
            fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("already-sent"));
        return stored && stored->state == MessageState::Sent;
    }(), 3000);

    bool lateCancelled = true;
    fixture.service->cancelSend(QStringLiteral("already-sent"), [&](Result<bool> result) {
        lateCancelled = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(!lateCancelled, 3000);
}

void TestMessageService::cancelWindowHoldsTheMessage()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.service->setCancelWindowMs(400);

    fixture.service->sendText(fixture.chatId, QStringLiteral("hold"), {},
                              QStringLiteral("held"));
    QTest::qWait(120);
    QCOMPARE(fixture.transport->sent.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 3000);
    QCOMPARE(fixture.transport->sent.first().clientMsgId, QStringLiteral("held"));
}

void TestMessageService::deliveryAndReadUpdateStatusWithoutDowngrade()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    fixture.service->sendText(fixture.chatId, QStringLiteral("status"), {},
                              QStringLiteral("m1"));
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
        return stored && stored->state == MessageState::Sent;
    }(), 3000);

    fixture.transport->deliverStatus(QStringLiteral("m1"), fixture.chatId, 777);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
        return stored && stored->state == MessageState::Delivered;
    }(), 3000);

    fixture.transport->deliverRead(fixture.chatId, {QStringLiteral("m1")});
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
        return stored && stored->state == MessageState::Read;
    }(), 3000);

    // A late delivery frame must not move the tick backwards.
    fixture.transport->deliverStatus(QStringLiteral("m1"), fixture.chatId, 777);
    QTest::qWait(150);
    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->state, MessageState::Read);
}

void TestMessageService::receivesMessageIncrementsUnreadAndMarksRead()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QJsonObject header;
    auto sealed = fixture.crypto.encrypt(fixture.chatId, QByteArrayLiteral("hi there"), &header);
    QVERIFY(sealed.isOk());
    header.insert(QStringLiteral("sender_login"), QStringLiteral("alice"));

    net::Envelope envelope;
    envelope.kind = QStringLiteral("text");
    envelope.clientMsgId = QStringLiteral("incoming-1");
    envelope.chatId = fixture.chatId;
    envelope.ciphertext = sealed.value();
    envelope.header = header;

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.transport->deliverIncoming(envelope);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId,
                                     QStringLiteral("incoming-1"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->body, QStringLiteral("hi there"));
    QCOMPARE(stored->state, MessageState::Received);
    QVERIFY(!stored->outgoing);

    const auto chat = fetchChat(*fixture.worker, fixture.chatId);
    QVERIFY(chat.has_value());
    QCOMPARE(chat->unreadCount, 1);

    int marked = -1;
    fixture.service->markChatRead(fixture.chatId, [&](Result<int> result) {
        marked = result.isOk() ? result.value() : -1;
    });
    QTRY_COMPARE_WITH_TIMEOUT(marked, 1, 3000);

    const auto read = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("incoming-1"));
    QVERIFY(read.has_value());
    QCOMPARE(read->state, MessageState::Read);
    QCOMPARE(fetchChat(*fixture.worker, fixture.chatId)->unreadCount, 0);
}

void TestMessageService::duplicateIncomingIsIgnored()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QJsonObject header;
    auto sealed = fixture.crypto.encrypt(fixture.chatId, QByteArrayLiteral("once"), &header);
    header.insert(QStringLiteral("sender_login"), QStringLiteral("alice"));
    net::Envelope envelope;
    envelope.kind = QStringLiteral("text");
    envelope.clientMsgId = QStringLiteral("dup");
    envelope.chatId = fixture.chatId;
    envelope.ciphertext = sealed.value();
    envelope.header = header;

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.transport->deliverIncoming(envelope);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);
    fixture.transport->deliverIncoming(envelope);
    QTest::qWait(200);

    QCOMPARE(added.count(), 1);
    QCOMPARE(messageCount(*fixture.worker, fixture.chatId), 1);
    QCOMPARE(fetchChat(*fixture.worker, fixture.chatId)->unreadCount, 1);
}

void TestMessageService::incomingCreatesMissingChat()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    const QString newChatId = ChatService::directChatId(QStringLiteral("matvey"),
                                                        QStringLiteral("dave"));
    QVERIFY(!fetchChat(*fixture.worker, newChatId).has_value());

    QJsonObject header;
    auto sealed = fixture.crypto.encrypt(newChatId, QByteArrayLiteral("first contact"), &header);
    QVERIFY(sealed.isOk());
    header.insert(QStringLiteral("sender_login"), QStringLiteral("dave"));

    net::Envelope envelope;
    envelope.kind = QStringLiteral("text");
    envelope.clientMsgId = QStringLiteral("incoming-new");
    envelope.chatId = newChatId;
    envelope.ciphertext = sealed.value();
    envelope.header = header;

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.transport->deliverIncoming(envelope);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 3000);

    const auto stored =
        fetchMessage(*fixture.worker, newChatId, QStringLiteral("incoming-new"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->body, QStringLiteral("first contact"));

    const auto chat = fetchChat(*fixture.worker, newChatId);
    QVERIFY(chat.has_value());
    QCOMPARE(chat->peerLogin, QStringLiteral("dave"));
    QCOMPARE(chat->unreadCount, 1);
}

void TestMessageService::keepsPerChatOrder()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.transport->mode = FakeTransport::Mode::Manual;

    fixture.service->sendText(fixture.chatId, QStringLiteral("first"), {},
                              QStringLiteral("m1"));
    fixture.service->sendText(fixture.chatId, QStringLiteral("second"), {},
                              QStringLiteral("m2"));

    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 3000);
    QCOMPARE(fixture.transport->sent.first().clientMsgId, QStringLiteral("m1"));
    QTest::qWait(150);
    QCOMPARE(fixture.transport->sent.size(), 1);

    fixture.transport->completeNext(true);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 2, 3000);
    QCOMPARE(fixture.transport->sent.at(1).clientMsgId, QStringLiteral("m2"));
    fixture.transport->completeNext(true);
}

void TestMessageService::retriesAfterNetworkFailure()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.service->setRetryDelays({5, 10});
    fixture.transport->mode = FakeTransport::Mode::NetworkFailure;

    QSignalSpy updated(fixture.service.get(), &MessageService::messageUpdated);
    fixture.service->sendText(fixture.chatId, QStringLiteral("retry"), {},
                              QStringLiteral("m1"));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.transport->sent.size() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.transport->sent.size() >= 2, 3000);
    QVERIFY(updated.count() >= 2);

    // The second attempt is in flight when sent.size() reaches 2; wait for the
    // failure handler to return the row to the queue instead of racing it.
    QTRY_VERIFY_WITH_TIMEOUT(
        ([&fixture] {
            const auto current =
                fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
            return current.has_value() && current->state == MessageState::Queued;
        })(),
        3000);
    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("m1"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->state, MessageState::Queued);
}

void TestMessageService::pendingOutboxSurvivesRestart()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.transport->online = false;
    fixture.service->sendText(fixture.chatId, QStringLiteral("offline"), {},
                              QStringLiteral("m1"));
    QTest::qWait(150);
    QCOMPARE(fixture.transport->sent.size(), 0);

    fixture.service.reset();
    fixture.transport.reset();
    fixture.worker->close();
    fixture.worker.reset();

    auto reopened = std::make_unique<storage::StorageWorker>();
    QVERIFY(reopened->open(fixture.dir.filePath(QStringLiteral("messages.db")), dbKey()).isOk());
    auto transport = std::make_unique<FakeTransport>();
    auto service = std::make_unique<MessageService>(reopened.get());
    service->setCryptoSession(&fixture.crypto);
    service->setTransport(transport.get());

    service->flushQueue();
    QTRY_COMPARE_WITH_TIMEOUT(transport->sent.size(), 1, 3000);
    QCOMPARE(transport->sent.first().clientMsgId, QStringLiteral("m1"));
}

void TestMessageService::rejectsSpoofedChatId()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    // The server says "mallory", but the envelope claims matvey<->alice's chat.
    QJsonObject header;
    auto sealed = fixture.crypto.encrypt(fixture.chatId, QByteArrayLiteral("injected"), &header);
    QVERIFY(sealed.isOk());
    header.insert(QStringLiteral("sender_login"), QStringLiteral("mallory"));

    net::Envelope envelope;
    envelope.kind = QStringLiteral("text");
    envelope.clientMsgId = QStringLiteral("spoofed");
    envelope.chatId = fixture.chatId;
    envelope.ciphertext = sealed.value();
    envelope.header = header;

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    QSignalSpy errors(fixture.service.get(), &MessageService::errorOccurred);
    fixture.transport->deliverIncoming(envelope);
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 3000);
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("chat_id_mismatch"));
    QCOMPARE(added.count(), 0);
    QVERIFY(!fetchMessage(*fixture.worker, fixture.chatId,
                          QStringLiteral("spoofed")).has_value());
}

QTEST_MAIN(TestMessageService)
#include "test_message_service.moc"
