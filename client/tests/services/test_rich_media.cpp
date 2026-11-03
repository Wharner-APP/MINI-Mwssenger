#include "core/BuildInfo.h"
#include "crypto/Sodium.h"
#include "domain/Media.h"
#include "services/ChatService.h"
#include "services/GifCatalog.h"
#include "services/MessageService.h"
#include "services/StickerService.h"
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
        if (worker->open(dir.filePath(QStringLiteral("rich.db")), dbKey()).isError()) {
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

    QString seedMedia(const QString &id, MediaKind kind = MediaKind::Photo)
    {
        MediaRecord record;
        record.mediaId = id;
        record.chatId = chatId;
        record.kind = kind;
        record.mime = kind == MediaKind::Video ? QStringLiteral("video/mp4")
                                               : QStringLiteral("image/png");
        record.name = id + QStringLiteral(".png");
        record.size = 4096;
        record.chunkSize = 1048560;
        record.fileHash = QString(64, QLatin1Char('a'));
        record.fileKey = QByteArray(32, 'k');
        record.nonceBase = QByteArray(16, 'n');
        record.width = 640;
        record.height = 480;
        record.state = QStringLiteral("uploaded");
        record.createdAt = 1;
        auto future = worker->submit([record](storage::Database &db) {
            return storage::MediaRepo(db).insert(record).isOk();
        });
        future.waitForFinished();
        return future.result() ? id : QString();
    }
};

} // namespace

class TestRichMedia : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        qRegisterMetaType<Message>("mini::Message");
    }

    void builtInStickerPacks();
    void installMinistickerPack();
    void stickerMessageTravels();
    void incomingStickerStored();
    void gifMessageTravels();
    void albumMessageTravelsAsOneBody();
    void incomingAlbumStoresEveryItem();
    void albumRejectsEmptyAndTooLarge();
};

void TestRichMedia::builtInStickerPacks()
{
    StickerService stickers;
    const auto packs = stickers.packs();
    QVERIFY2(packs.size() >= 3, qPrintable(QString::number(packs.size())));
    for (const StickerPack &pack : packs) {
        QVERIFY(!pack.id.isEmpty());
        QVERIFY(!pack.title.isEmpty());
        QVERIFY2(pack.stickers.size() >= 8 && pack.stickers.size() <= 16,
                 qPrintable(pack.id));
        const QPixmap pixmap =
            stickers.stickerPixmap(pack.id, pack.stickers.first().id, 160);
        QVERIFY2(!pixmap.isNull(), qPrintable(pack.id));
    }
}

void TestRichMedia::installMinistickerPack()
{
    QTemporaryDir userDir;
    QVERIFY(userDir.isValid());
    StickerService stickers;
    stickers.setUserDir(userDir.path());
    const int before = stickers.packs().size();

    QString installed;
    const Result<bool> result =
        stickers.install(QStringLiteral(":/stickers/sample.ministicker"), &installed);
    QVERIFY2(result.isOk(), qPrintable(result.error()));
    QCOMPARE(installed, QStringLiteral("mini.sample"));
    QCOMPARE(stickers.packs().size(), before + 1);
    QVERIFY(stickers.isInstalled(QStringLiteral("mini.sample")));
    QVERIFY(!stickers
                 .stickerPixmap(QStringLiteral("mini.sample"),
                                QStringLiteral("sample-happy"), 160)
                 .isNull());

    const Result<bool> removed = stickers.remove(QStringLiteral("mini.sample"));
    QVERIFY2(removed.isOk(), qPrintable(removed.error()));
    QVERIFY(!stickers.pack(QStringLiteral("mini.sample")).has_value());
    QCOMPARE(stickers.packs().size(), before);

    const Result<bool> builtIn = stickers.remove(QStringLiteral("mini.cats"));
    QVERIFY(builtIn.isError());
}

void TestRichMedia::stickerMessageTravels()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);

    const QString clientId = fixture.service->sendSticker(
        fixture.chatId, QStringLiteral("mini.cats"), QStringLiteral("cats-happy"),
        QStringLiteral("Радость"), {}, QStringLiteral("sticker-1"));
    QCOMPARE(clientId, QStringLiteral("sticker-1"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, clientId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Sticker);
    QCOMPARE(stored->mediaId, QStringLiteral("mini.cats/cats-happy"));
    QCOMPARE(stored->body, QStringLiteral("Радость"));

    const net::Envelope &envelope = fixture.transport->sent.first();
    auto plain = fixture.crypto.decrypt(envelope.chatId, envelope.ciphertext,
                                        envelope.header);
    QVERIFY(plain.isOk());
    const RichBody rich = decodeRichBody(plain.value());
    QVERIFY(rich.kind == MediaKind::Sticker);
    QCOMPARE(rich.sticker.packId, QStringLiteral("mini.cats"));
    QCOMPARE(rich.sticker.stickerId, QStringLiteral("cats-happy"));
    QCOMPARE(rich.sticker.title, QStringLiteral("Радость"));
}

void TestRichMedia::incomingStickerStored()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendSticker(fixture.chatId, QStringLiteral("mini.dogs"),
                                 QStringLiteral("dogs-love"), QStringLiteral("Любовь"),
                                 {}, QStringLiteral("sticker-out"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    net::Envelope incoming = fixture.transport->sent.first();
    incoming.clientMsgId = QStringLiteral("sticker-in");
    incoming.header.insert(QStringLiteral("sender_login"), QStringLiteral("alice"));
    incoming.header.insert(QStringLiteral("ts"), 1700000000000.0);
    fixture.transport->deliverIncoming(incoming);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 2, 5000);

    const auto stored =
        fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("sticker-in"));
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Sticker);
    QCOMPARE(stored->mediaId, QStringLiteral("mini.dogs/dogs-love"));
    QCOMPARE(stored->body, QStringLiteral("Любовь"));
    QVERIFY(!stored->outgoing);
}

void TestRichMedia::gifMessageTravels()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QVERIFY(!GifCatalog::instance().items().isEmpty());
    const GifItem first = GifCatalog::instance().items().first();
    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);

    const QString clientId = fixture.service->sendGif(
        fixture.chatId, first.id, first.title, {}, QStringLiteral("gif-1"));
    QCOMPARE(clientId, QStringLiteral("gif-1"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, clientId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Gif);
    QCOMPARE(stored->mediaId, first.id);

    auto plain = fixture.crypto.decrypt(fixture.transport->sent.first().chatId,
                                        fixture.transport->sent.first().ciphertext,
                                        fixture.transport->sent.first().header);
    QVERIFY(plain.isOk());
    const RichBody rich = decodeRichBody(plain.value());
    QVERIFY(rich.kind == MediaKind::Gif);
    QCOMPARE(rich.gif.gifId, first.id);
}

void TestRichMedia::albumMessageTravelsAsOneBody()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    const QString first = fixture.seedMedia(QStringLiteral("up_album_1"));
    const QString second = fixture.seedMedia(QStringLiteral("up_album_2"));
    const QString third = fixture.seedMedia(QStringLiteral("up_album_3"),
                                            MediaKind::Video);
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    const QStringList ids{first, second, third};
    const QString clientId = fixture.service->sendAlbum(
        fixture.chatId, ids, QStringLiteral("три кадра"), {},
        QStringLiteral("album-1"));
    QCOMPARE(clientId, QStringLiteral("album-1"));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    const auto stored = fetchMessage(*fixture.worker, fixture.chatId, clientId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Album);
    QCOMPARE(splitMediaIds(stored->mediaId), ids);
    QCOMPARE(stored->body, QStringLiteral("три кадра"));
    QCOMPARE(stored->mediaSize, qint64(4096 * 3));

    auto plain = fixture.crypto.decrypt(fixture.transport->sent.first().chatId,
                                        fixture.transport->sent.first().ciphertext,
                                        fixture.transport->sent.first().header);
    QVERIFY(plain.isOk());
    AlbumBody album;
    QVERIFY(decodeAlbumBody(plain.value(), &album));
    QCOMPARE(album.caption, QStringLiteral("три кадра"));
    QCOMPARE(album.items.size(), 3);
    QCOMPARE(album.items.at(0).fileId, first);
    QVERIFY(album.items.at(2).kind == MediaKind::Video);
}

void TestRichMedia::incomingAlbumStoresEveryItem()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    const QString first = fixture.seedMedia(QStringLiteral("up_in_1"));
    const QString second = fixture.seedMedia(QStringLiteral("up_in_2"));
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    QSignalSpy added(fixture.service.get(), &MessageService::messageAdded);
    fixture.service->sendAlbum(fixture.chatId, {first, second},
                               QStringLiteral("альбом"), {},
                               QStringLiteral("album-out"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.transport->sent.size(), 1, 5000);

    net::Envelope incoming = fixture.transport->sent.first();
    incoming.clientMsgId = QStringLiteral("album-in");
    incoming.header.insert(QStringLiteral("sender_login"), QStringLiteral("alice"));
    incoming.header.insert(QStringLiteral("ts"), 1700000000000.0);
    fixture.transport->deliverIncoming(incoming);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 2, 5000);

    const auto stored =
        fetchMessage(*fixture.worker, fixture.chatId, QStringLiteral("album-in"));
    QVERIFY(stored.has_value());
    QVERIFY(stored->kind == MediaKind::Album);
    const QStringList expectedIds{first, second};
    QCOMPARE(splitMediaIds(stored->mediaId), expectedIds);
    QCOMPARE(stored->body, QStringLiteral("альбом"));

    auto future = fixture.worker->submit([first, second](storage::Database &db) {
        storage::MediaRepo repo(db);
        const auto one = repo.get(first);
        const auto two = repo.get(second);
        return one.isOk() && one.value().has_value() && two.isOk()
            && two.value().has_value();
    });
    future.waitForFinished();
    QVERIFY(future.result());
}

void TestRichMedia::albumRejectsEmptyAndTooLarge()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QSignalSpy errors(fixture.service.get(), &MessageService::errorOccurred);

    QVERIFY(fixture.service->sendAlbum(fixture.chatId, {}, QStringLiteral("x")).isEmpty());
    QCOMPARE(errors.count(), 1);

    QStringList many;
    for (int i = 0; i < 11; ++i) {
        many.append(QStringLiteral("up_many_%1").arg(i));
    }
    QVERIFY(fixture.service->sendAlbum(fixture.chatId, many, QStringLiteral("x")).isEmpty());
    QCOMPARE(errors.count(), 2);
}

QTEST_MAIN(TestRichMedia)
#include "test_rich_media.moc"
