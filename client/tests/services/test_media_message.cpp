#include "crypto/Sodium.h"
#include "domain/Media.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/Database.h"
#include "storage/MediaRepo.h"
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
    explicit FakeTransport(QObject *parent = nullptr) : MessageTransport(parent) {}

    void send(const QList<net::Envelope> &envelopes, SendCallback done) override
    {
        for (const net::Envelope &envelope : envelopes) {
            sent.append(envelope);
        }
        net::SendResult result;
        for (const net::Envelope &envelope : envelopes) {
            result.accepted.append(envelope.clientMsgId);
        }
        done(net::ApiResult<net::SendResult>::ok(result));
    }

    void deliverIncoming(const net::Envelope &envelope) { emit messageReceived(envelope); }

    QList<net::Envelope> sent;
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
        if (!dir.isValid() || !crypto::Sodium::init()) {
            return false;
        }
        worker = std::make_unique<storage::StorageWorker>();
        if (worker->open(dir.filePath(QStringLiteral("media.db")), dbKey()).isError()) {
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

    QString seedMedia()
    {
        MediaRecord record;
        record.mediaId = QStringLiteral("up_0123456789abcdef0123");
        record.chatId = chatId;
        record.kind = MediaKind::Photo;
        record.mime = QStringLiteral("image/png");
        record.name = QStringLiteral("photo.png");
        record.size = 4096;
        record.chunkSize = 1048560;
        record.fileHash = QString(64, QLatin1Char('a'));
        record.fileKey = QByteArray(32, 'k');
        record.nonceBase = QByteArray(16, 'n');
        record.width = 640;
        record.height = 480;
        record.state = QStringLiteral("uploaded");
        record.createdAt = 1;
        const QByteArray thumb = QByteArray::fromHex("89504e470d0a1a0a");
        auto future = worker->submit([record, thumb](storage::Database &db) {
            storage::MediaRepo repo(db);
            repo.insert(record);
            repo.setMeta(record.mediaId, QStringLiteral("thumb_b64"),
                         QString::fromLatin1(thumb.toBase64()));
            return true;
        });
        future.waitForFinished();
        return record.mediaId;
    }
};

} // namespace

class TestMediaMessage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<Message>("mini::Message"); }

    void photoMessageTravelsAsMediaEnvelope();
    void incomingPhotoStoresMediaAndCaption();
};

void TestMediaMessage::photoMessageTravelsAsMediaEnvelope()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    const QString mediaId = fixture.seedMedia();

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    const QString clientId = fixture.service->sendMedia(fixture.chatId, mediaId,
                                                        QStringLiteral("смотри"),
                                                        {}, QStringLiteral("media-1"));
    QCOMPARE(clientId, QStringLiteral("media-1"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, clientId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Photo);
    QCOMPARE(stored->mediaId, mediaId);
    QCOMPARE(stored->body, QStringLiteral("смотри"));
    QCOMPARE(stored->mediaName, QStringLiteral("photo.png"));
    QCOMPARE(stored->mediaSize, 4096);
    QCOMPARE(stored->mediaDurationMs, 0);

    const net::Envelope &envelope = fixture.transport->sent.first();
    auto plain = fixture.crypto.decrypt(envelope.chatId, envelope.ciphertext, envelope.header);
    QVERIFY(plain.isOk());
    // The decrypted body is the media envelope, not the bare caption.
    MediaKind kind = MediaKind::Text;
    QString caption;
    MediaWire wire;
    QVERIFY(decodeMediaBody(plain.value(), &kind, &caption, &wire));
    QVERIFY(kind == MediaKind::Photo);
    QCOMPARE(caption, QStringLiteral("смотри"));
    QCOMPARE(wire.fileId, mediaId);
    QCOMPARE(wire.fileKey, QByteArray(32, 'k'));
    QCOMPARE(wire.nonceBase, QByteArray(16, 'n'));
    QCOMPARE(wire.name, QStringLiteral("photo.png"));
    QVERIFY(!wire.thumb.isEmpty());
}

void TestMediaMessage::incomingPhotoStoresMediaAndCaption()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    const QString mediaId = fixture.seedMedia();

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendMedia(fixture.chatId, mediaId, QStringLiteral("подпись"),
                               {}, QStringLiteral("media-2"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    net::Envelope incoming = fixture.transport->sent.first();
    incoming.clientMsgId = QStringLiteral("incoming-1");
    incoming.header.insert(QStringLiteral("sender_login"), QStringLiteral("alice"));
    incoming.header.insert(QStringLiteral("ts"), 1700000000000.0);
    fixture.transport->deliverIncoming(incoming);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 2, 5000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId,
                                     QStringLiteral("incoming-1"));
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Photo);
    QCOMPARE(stored->mediaId, mediaId);
    QCOMPARE(stored->body, QStringLiteral("подпись"));
    QCOMPARE(stored->mediaName, QStringLiteral("photo.png"));
    QCOMPARE(stored->outgoing, false);

    auto future = fixture.worker->submit([mediaId](storage::Database &db) {
        storage::MediaRepo repo(db);
        const auto record = repo.get(mediaId);
        const auto thumb = repo.meta(mediaId, QStringLiteral("thumb_b64"));
        return QPair<bool, QString>(record.isOk() && record.value().has_value(),
                                    thumb.isOk() ? thumb.value() : QString());
    });
    future.waitForFinished();
    QVERIFY(future.result().first);
    QVERIFY(!future.result().second.isEmpty());
}

QTEST_MAIN(TestMediaMessage)
#include "test_media_message.moc"
