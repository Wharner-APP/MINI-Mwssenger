#include "crypto/Sodium.h"
#include "domain/Media.h"
#include "services/MediaCache.h"
#include "services/MediaCrypto.h"
#include "services/MediaService.h"
#include "services/MediaTransport.h"
#include "storage/Database.h"
#include "storage/MediaRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

class FakeMediaTransport : public MediaTransport
{
public:
    void initUpload(qint64 totalSize, qint64 chunkSize, InitCallback done) override
    {
        ++initCalls;
        session.uploadId = QStringLiteral("up_%1").arg(++nextId, 20, 16, QLatin1Char('0'));
        session.totalSize = totalSize;
        session.chunkSize = chunkSize;
        session.chunkCount = static_cast<int>((totalSize + chunkSize - 1) / chunkSize);
        session.expiresAt = 0;
        done(net::ApiResult<net::MediaUploadSession>::ok(session));
    }

    void putChunk(const QString &uploadId, int idx, const QByteArray &data,
                  ChunkCallback done) override
    {
        if (putFailuresLeft > 0) {
            --putFailuresLeft;
            done(net::ApiResult<net::MediaChunkResult>::fail(
                net::ApiError::network(QStringLiteral("offline"))));
            return;
        }
        stored[uploadId].insert(idx, data);
        requestedPuts.append(idx);
        net::MediaChunkResult result;
        result.uploadId = uploadId;
        result.idx = idx;
        result.chunkCount = session.chunkCount;
        done(net::ApiResult<net::MediaChunkResult>::ok(result));
    }

    void complete(const QString &uploadId, CompleteCallback done) override
    {
        completed = uploadId;
        net::MediaCompleteResult result;
        result.uploadId = uploadId;
        result.chunkCount = stored.value(uploadId).size();
        done(net::ApiResult<net::MediaCompleteResult>::ok(result));
    }

    void getChunk(const QString &uploadId, int idx, BytesCallback done) override
    {
        requestedGets.append(idx);
        const auto chunks = stored.value(uploadId);
        if (!chunks.contains(idx)) {
            done(net::ApiResult<QByteArray>::fail(net::ApiError::fromStatus(
                404, QByteArray(R"({"error":{"code":"media_not_found"}})"))));
            return;
        }
        done(net::ApiResult<QByteArray>::ok(chunks.value(idx)));
    }

    net::MediaUploadSession session;
    QHash<QString, QHash<int, QByteArray>> stored;
    QList<int> requestedPuts;
    QList<int> requestedGets;
    QString completed;
    int initCalls = 0;
    int nextId = 0;
    int putFailuresLeft = 0;
};

struct Fixture
{
    QTemporaryDir dir;
    std::unique_ptr<storage::StorageWorker> worker;
    std::unique_ptr<FakeMediaTransport> transport;
    std::unique_ptr<MediaService> service;

    bool open()
    {
        if (!dir.isValid() || !crypto::Sodium::init()) {
            return false;
        }
        worker = std::make_unique<storage::StorageWorker>();
        if (worker->open(dir.filePath(QStringLiteral("media.db")), dbKey()).isError()) {
            return false;
        }
        transport = std::make_unique<FakeMediaTransport>();
        service = std::make_unique<MediaService>();
        service->setStorage(worker.get());
        service->setTransport(transport.get());
        service->setCacheRoot(dir.filePath(QStringLiteral("media")));
        service->setRetryDelays({0, 0, 0});
        return true;
    }

    QString writeFile(const QString &name, const QByteArray &data)
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(data);
        file.close();
        return path;
    }
};

} // namespace

class TestMediaService : public QObject
{
    Q_OBJECT

private slots:
    void preparePhotoProducesThumbsAndHash();
    void prepareFileDetectsTypeByMagic();
    void prepareRejectsExecutablesAndOversizedPhotos();
    void uploadEncryptsIntoOneMiBChunks();
    void downloadRoundTripsAndResumes();
    void uploadRetriesAfterNetworkFailure();
};

void TestMediaService::preparePhotoProducesThumbsAndHash()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QImage image(640, 480, QImage::Format_RGB32);
    image.fill(QColor(200, 40, 40));
    const QString path = fixture.dir.filePath(QStringLiteral("photo.png"));
    QVERIFY(image.save(path));

    bool finished = false;
    Result<MediaPreparation> captured = Result<MediaPreparation>::error(QStringLiteral("none"));
    fixture.service->prepareFile(path, [&](Result<MediaPreparation> result) {
        captured = result;
        finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(captured.isOk(), qPrintable(captured.error()));

    const MediaPreparation prep = captured.value();
    QVERIFY(prep.kind == MediaKind::Photo);
    QCOMPARE(prep.mime, QStringLiteral("image/png"));
    QCOMPARE(prep.width, 640);
    QCOMPARE(prep.height, 480);
    QCOMPARE(prep.fileKey.size(), 32);
    QCOMPARE(prep.nonceBase.size(), 16);
    QCOMPARE(prep.fileHash.size(), 64);
    QVERIFY(!prep.thumb320.isEmpty());
    QVERIFY(!prep.preview1280.isEmpty());
    QVERIFY(QFileInfo::exists(prep.sourcePath));
}

void TestMediaService::prepareFileDetectsTypeByMagic()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    // A real JPEG saved under a .txt name: magic bytes win (§40.2).
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(QColor(20, 80, 160));
    const QString path = fixture.dir.filePath(QStringLiteral("photo.txt"));
    QVERIFY(image.save(path, "JPEG"));

    bool finished = false;
    Result<MediaPreparation> captured = Result<MediaPreparation>::error(QStringLiteral("none"));
    fixture.service->prepareFile(path, [&](Result<MediaPreparation> result) {
        captured = result;
        finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY(captured.isOk());
    QCOMPARE(captured.value().mime, QStringLiteral("image/jpeg"));
    QCOMPARE(captured.value().warning, QStringLiteral("content_mismatch"));
}

void TestMediaService::prepareRejectsExecutablesAndOversizedPhotos()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    const QString exe = fixture.writeFile(QStringLiteral("setup.exe"),
                                          QByteArrayLiteral("MZ fake"));
    bool finished = false;
    QString error;
    fixture.service->prepareFile(exe, [&](Result<MediaPreparation> result) {
        error = result.error();
        finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCOMPARE(error, QStringLiteral("executable_forbidden"));

    // 21 MiB of JPEG magic: over the 20 MiB photo limit.
    QByteArray big = QByteArray::fromHex("ffd8ffe000104a4649460001");
    big.resize(21 * 1024 * 1024, '\0');
    const QString huge = fixture.writeFile(QStringLiteral("huge.jpg"), big);
    finished = false;
    error.clear();
    fixture.service->prepareFile(huge, [&](Result<MediaPreparation> result) {
        error = result.error();
        finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCOMPARE(error, QStringLiteral("media_too_large"));
}

void TestMediaService::uploadEncryptsIntoOneMiBChunks()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(QColor(10, 120, 200));
    const QString path = fixture.dir.filePath(QStringLiteral("upload.png"));
    QVERIFY(image.save(path));

    bool prepared = false;
    MediaPreparation prep;
    fixture.service->prepareFile(path, [&](Result<MediaPreparation> result) {
        if (result.isOk()) {
            prep = result.value();
        }
        prepared = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(prepared, 10000);
    QVERIFY(!prep.fileHash.isEmpty());

    bool uploaded = false;
    QString mediaId;
    QString error;
    QSignalSpy progress(fixture.service.get(), &MediaService::uploadProgress);
    fixture.service->upload(QStringLiteral("direct:matvey:alice"), prep,
                            [&](Result<QString> result) {
                                uploaded = true;
                                if (result.isOk()) {
                                    mediaId = result.value();
                                } else {
                                    error = result.error();
                                }
                            });
    QTRY_VERIFY_WITH_TIMEOUT(uploaded, 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(mediaId.startsWith(QLatin1String("up_")));
    QVERIFY(progress.count() > 0);
    QVERIFY(!fixture.transport->completed.isEmpty());

    const auto chunks = fixture.transport->stored.value(mediaId);
    QCOMPARE(chunks.size(), MediaCrypto::chunkCount(prep.size));
    qint64 total = 0;
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        total += it.value().size();
        if (it.key() < chunks.size() - 1) {
            QCOMPARE(it.value().size(), MediaCrypto::kChunkCipher);
        }
    }
    QCOMPARE(total, MediaCrypto::cipherSize(prep.size));

    auto future = fixture.worker->submit([mediaId](storage::Database &db) {
        return storage::MediaRepo(db).get(mediaId);
    });
    future.waitForFinished();
    const auto record = future.result();
    QVERIFY(record.isOk());
    QVERIFY(record.value().has_value());
    QCOMPARE(record.value()->state, QStringLiteral("uploaded"));
    QCOMPARE(record.value()->fileKey, prep.fileKey);
}

void TestMediaService::downloadRoundTripsAndResumes()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    // 2.5 MiB of non-image data: three chunks, no thumbnail work.
    QByteArray payload(static_cast<int>(2.5 * 1024 * 1024), '\0');
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>((i * 31) & 0xFF);
    }
    const QString path = fixture.writeFile(QStringLiteral("archive.bin"), payload);

    bool prepared = false;
    MediaPreparation prep;
    fixture.service->prepareFile(path, [&](Result<MediaPreparation> result) {
        if (result.isOk()) {
            prep = result.value();
        }
        prepared = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(prepared, 10000);
    QVERIFY(prep.kind == MediaKind::File);

    bool uploaded = false;
    QString mediaId;
    fixture.service->upload(QStringLiteral("direct:matvey:alice"), prep,
                            [&](Result<QString> result) {
                                mediaId = result.value();
                                uploaded = true;
                            });
    QTRY_VERIFY_WITH_TIMEOUT(uploaded, 10000);
    QVERIFY(!mediaId.isEmpty());

    bool downloaded = false;
    QString localPath;
    QString error;
    fixture.service->download(mediaId, [&](Result<QString> result) {
        downloaded = true;
        if (result.isOk()) {
            localPath = result.value();
        } else {
            error = result.error();
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(downloaded, 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(localPath));

    QFile local(localPath);
    QVERIFY(local.open(QIODevice::ReadOnly));
    QCOMPARE(local.readAll(), payload);
    local.close();

    // Simulate an interrupted download: the first chunk is already on disk.
    fixture.transport->requestedGets.clear();
    const QString cachePath =
        fixture.service->cache()->cacheDir() + QLatin1Char('/') + mediaId
        + QStringLiteral(".bin");
    QFile partial(cachePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write(payload.left(MediaCrypto::kChunkPlain));
    partial.close();
    auto reset = fixture.worker->submit([mediaId](storage::Database &db) {
        return storage::MediaRepo(db)
            .updateState(mediaId, QStringLiteral("downloading"), -1, 1, QString())
            .isOk();
    });
    reset.waitForFinished();

    downloaded = false;
    error.clear();
    fixture.service->download(mediaId, [&](Result<QString> result) {
        downloaded = true;
        if (result.isError()) {
            error = result.error();
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(downloaded, 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!fixture.transport->requestedGets.contains(0));
    QVERIFY(fixture.transport->requestedGets.contains(1));
}

void TestMediaService::uploadRetriesAfterNetworkFailure()
{
    Fixture fixture;
    QVERIFY(fixture.open());

    QByteArray payload(200 * 1024, 'x');
    const QString path = fixture.writeFile(QStringLiteral("retry.bin"), payload);

    bool prepared = false;
    MediaPreparation prep;
    fixture.service->prepareFile(path, [&](Result<MediaPreparation> result) {
        if (result.isOk()) {
            prep = result.value();
        }
        prepared = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(prepared, 10000);

    fixture.transport->putFailuresLeft = 1;
    bool uploaded = false;
    QString error;
    fixture.service->upload(QStringLiteral("direct:matvey:alice"), prep,
                            [&](Result<QString> result) {
                                uploaded = true;
                                error = result.error();
                            });
    QTRY_VERIFY_WITH_TIMEOUT(uploaded, 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!fixture.transport->completed.isEmpty());
}

QTEST_MAIN(TestMediaService)
#include "test_media_service.moc"
