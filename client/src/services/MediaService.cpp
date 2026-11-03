#include "services/MediaService.h"

#include "services/MediaCache.h"
#include "services/MediaCrypto.h"
#include "services/MediaTransport.h"
#include "storage/Database.h"
#include "storage/MediaRepo.h"
#include "storage/StorageWorker.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

namespace mini::services {

namespace {

constexpr qint64 kPhotoLimit = 20LL * 1024 * 1024;
constexpr qint64 kVideoLimit = 200LL * 1024 * 1024;
constexpr qint64 kFileLimit = 200LL * 1024 * 1024;
constexpr qint64 kPremiumLimit = 2048LL * 1024 * 1024;
constexpr int kMaxImageSide = 2560;
constexpr int kThumbSide = 320;
constexpr int kPreviewSide = 1280;

QByteArray readHead(const QString &path, int count = 16)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.read(count);
}

bool isForbiddenExtension(const QString &suffix)
{
    static const QSet<QString> forbidden{
        QStringLiteral("exe"), QStringLiteral("dll"), QStringLiteral("bat"),
        QStringLiteral("cmd"), QStringLiteral("ps1"), QStringLiteral("msi"),
        QStringLiteral("scr"), QStringLiteral("vbs"), QStringLiteral("js")};
    return forbidden.contains(suffix.toLower());
}

struct Detection
{
    QString mime;
    MediaKind kind = MediaKind::File;
};

Detection detectMagic(const QByteArray &head)
{
    if (head.size() >= 3 && static_cast<unsigned char>(head[0]) == 0xFF
        && static_cast<unsigned char>(head[1]) == 0xD8
        && static_cast<unsigned char>(head[2]) == 0xFF) {
        return {QStringLiteral("image/jpeg"), MediaKind::Photo};
    }
    if (head.startsWith("\x89PNG\r\n\x1a\n")) {
        return {QStringLiteral("image/png"), MediaKind::Photo};
    }
    if (head.startsWith("GIF8")) {
        return {QStringLiteral("image/gif"), MediaKind::Photo};
    }
    if (head.startsWith("RIFF") && head.mid(8, 4) == "WEBP") {
        return {QStringLiteral("image/webp"), MediaKind::Photo};
    }
    if (head.startsWith("BM")) {
        return {QStringLiteral("image/bmp"), MediaKind::Photo};
    }
    if (head.size() >= 12 && head.mid(4, 4) == "ftyp") {
        const QByteArray brand = head.mid(8, 4);
        if (brand == "qt  ") {
            return {QStringLiteral("video/quicktime"), MediaKind::Video};
        }
        return {QStringLiteral("video/mp4"), MediaKind::Video};
    }
    if (head.startsWith("\x1a\x45\xdf\xa3")) {
        return {QStringLiteral("video/webm"), MediaKind::Video};
    }
    if (head.startsWith("%PDF")) {
        return {QStringLiteral("application/pdf"), MediaKind::File};
    }
    if (head.startsWith("PK\x03\x04")) {
        return {QStringLiteral("application/zip"), MediaKind::File};
    }
    if (head.startsWith("Rar!")) {
        return {QStringLiteral("application/vnd.rar"), MediaKind::File};
    }
    if (head.startsWith("7z\xbc\xaf")) {
        return {QStringLiteral("application/x-7z-compressed"), MediaKind::File};
    }
    if (head.startsWith("MZ") || head.startsWith("\x7f" "ELF")) {
        return {QStringLiteral("application/x-msdownload"), MediaKind::File};
    }
    return {};
}

Detection detectByExtension(const QString &suffix)
{
    static const QHash<QString, Detection> table{
        {QStringLiteral("jpg"), {QStringLiteral("image/jpeg"), MediaKind::Photo}},
        {QStringLiteral("jpeg"), {QStringLiteral("image/jpeg"), MediaKind::Photo}},
        {QStringLiteral("png"), {QStringLiteral("image/png"), MediaKind::Photo}},
        {QStringLiteral("gif"), {QStringLiteral("image/gif"), MediaKind::Photo}},
        {QStringLiteral("webp"), {QStringLiteral("image/webp"), MediaKind::Photo}},
        {QStringLiteral("bmp"), {QStringLiteral("image/bmp"), MediaKind::Photo}},
        {QStringLiteral("mp4"), {QStringLiteral("video/mp4"), MediaKind::Video}},
        {QStringLiteral("m4v"), {QStringLiteral("video/mp4"), MediaKind::Video}},
        {QStringLiteral("mov"), {QStringLiteral("video/quicktime"), MediaKind::Video}},
        {QStringLiteral("webm"), {QStringLiteral("video/webm"), MediaKind::Video}},
        {QStringLiteral("mkv"), {QStringLiteral("video/x-matroska"), MediaKind::Video}},
        {QStringLiteral("pdf"), {QStringLiteral("application/pdf"), MediaKind::File}},
        {QStringLiteral("txt"), {QStringLiteral("text/plain"), MediaKind::File}},
        {QStringLiteral("zip"), {QStringLiteral("application/zip"), MediaKind::File}},
        {QStringLiteral("rar"), {QStringLiteral("application/vnd.rar"), MediaKind::File}},
        {QStringLiteral("7z"), {QStringLiteral("application/x-7z-compressed"), MediaKind::File}},
        {QStringLiteral("doc"), {QStringLiteral("application/msword"), MediaKind::File}},
        {QStringLiteral("docx"),
         {QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"),
          MediaKind::File}},
        {QStringLiteral("odt"), {QStringLiteral("application/vnd.oasis.opendocument.text"),
                                 MediaKind::File}},
        {QStringLiteral("mp3"), {QStringLiteral("audio/mpeg"), MediaKind::File}},
        {QStringLiteral("ogg"), {QStringLiteral("audio/ogg"), MediaKind::File}},
        {QStringLiteral("wav"), {QStringLiteral("audio/wav"), MediaKind::File}},
    };
    return table.value(suffix.toLower(), Detection{});
}

qint64 limitFor(MediaKind kind, bool premium)
{
    switch (kind) {
    case MediaKind::Photo:
        return kPhotoLimit;
    case MediaKind::Video:
        return premium ? kPremiumLimit : kVideoLimit;
    case MediaKind::File:
    case MediaKind::Text:
        return premium ? kPremiumLimit : kFileLimit;
    }
    return kFileLimit;
}

QByteArray encodeImage(const QImage &image, int quality, QString *extension)
{
    const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    QString format;
    if (formats.contains("webp")) {
        format = QStringLiteral("webp");
    } else if (image.hasAlphaChannel()) {
        format = QStringLiteral("png");
    } else {
        format = QStringLiteral("jpeg");
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    QImageWriter writer(&buffer, format.toLatin1());
    writer.setQuality(quality);
    if (!writer.write(image)) {
        return {};
    }
    if (extension) {
        *extension = format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
    }
    return bytes;
}

QImage scaled(const QImage &image, int side)
{
    if (image.width() <= side && image.height() <= side) {
        return image;
    }
    return image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

template <typename T>
void watch(QObject *owner, QFuture<T> future, std::function<void(T)> done)
{
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
                         T value = watcher->result();
                         watcher->deleteLater();
                         done(std::move(value));
                     });
    watcher->setFuture(future);
}

} // namespace

MediaService::MediaService(QObject *parent) : QObject(parent)
{
    m_cache = new MediaCache();
}

MediaService::~MediaService()
{
    delete m_cache;
}

void MediaService::setStorage(storage::StorageWorker *storage)
{
    m_storage = storage;
}

void MediaService::setTransport(MediaTransport *transport)
{
    m_transport = transport;
}

void MediaService::setCacheRoot(const QString &root)
{
    m_cache->setRoot(root);
    m_cache->ensureDirs();
}

void MediaService::setPremium(bool premium)
{
    m_premium = premium;
}

void MediaService::setRetryDelays(const QVector<int> &milliseconds)
{
    m_retryDelays = milliseconds;
}

void MediaService::prepareFile(const QString &path, PrepareDone done, bool asFile)
{
    const bool premium = m_premium;
    const QString outgoingDir = QDir(m_cache->root()).filePath(QStringLiteral("outgoing"));
    auto self = QPointer<MediaService>(this);
    auto future = QtConcurrent::run([path, premium, outgoingDir, asFile]() -> Result<MediaPreparation> {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return Result<MediaPreparation>::error(QStringLiteral("file_not_found"));
        }
        const QString suffix = info.suffix().toLower();
        if (isForbiddenExtension(suffix)) {
            return Result<MediaPreparation>::error(QStringLiteral("executable_forbidden"));
        }
        const qint64 originalSize = info.size();
        if (originalSize <= 0) {
            return Result<MediaPreparation>::error(QStringLiteral("file_empty"));
        }

        const QByteArray head = readHead(path);
        Detection detected = detectMagic(head);
        const Detection byExtension = detectByExtension(suffix);
        QString warning;
        if (detected.mime.isEmpty()) {
            detected = byExtension;
        } else if (!byExtension.mime.isEmpty() && byExtension.kind != detected.kind) {
            warning = QStringLiteral("content_mismatch");
        }
        if (detected.mime.isEmpty()) {
            detected = {QStringLiteral("application/octet-stream"), MediaKind::File};
        }
        if (asFile) {
            detected.kind = MediaKind::File;
        }

        if (originalSize > limitFor(detected.kind, premium)) {
            return Result<MediaPreparation>::error(QStringLiteral("media_too_large"));
        }

        MediaPreparation prep;
        prep.sourcePath = path;
        prep.name = info.fileName();
        prep.kind = detected.kind;
        prep.mime = detected.mime;
        prep.size = originalSize;
        prep.warning = warning;
        prep.fileKey = MediaCrypto::randomKey();
        prep.nonceBase = MediaCrypto::randomNonce();

        if (prep.kind == MediaKind::Photo && detected.mime != QLatin1String("image/gif")) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize original = reader.size();
            if (original.isValid()
                && (original.width() > kMaxImageSide || original.height() > kMaxImageSide)) {
                reader.setScaledSize(original.scaled(kMaxImageSide, kMaxImageSide,
                                                     Qt::KeepAspectRatio));
            }
            const QImage image = reader.read();
            if (image.isNull()) {
                return Result<MediaPreparation>::error(QStringLiteral("image_decode_failed"));
            }
            prep.width = image.width();
            prep.height = image.height();
            QString thumbExtension;
            prep.thumb320 = encodeImage(scaled(image, kThumbSide), 80, &thumbExtension);
            QString previewExtension;
            prep.preview1280 = encodeImage(scaled(image, kPreviewSide), 85, &previewExtension);
            if (prep.thumb320.isEmpty()) {
                return Result<MediaPreparation>::error(QStringLiteral("thumbnail_failed"));
            }
            prep.thumbExtension = thumbExtension;

            const bool recompress = originalSize > 2LL * 1024 * 1024
                || (original.isValid()
                    && (original.width() > kMaxImageSide || original.height() > kMaxImageSide));
            if (recompress) {
                QDir().mkpath(outgoingDir);
                const bool alpha = image.hasAlphaChannel();
                const QString extension = alpha ? QStringLiteral("png") : QStringLiteral("jpg");
                const QString target = QDir(outgoingDir).filePath(
                    QStringLiteral("outgoing-%1.%2")
                        .arg(QDateTime::currentMSecsSinceEpoch())
                        .arg(extension));
                QImageWriter writer(target);
                writer.setQuality(alpha ? 100 : 88);
                if (writer.write(image)) {
                    const QFileInfo compressed(target);
                    if (compressed.size() > 0 && compressed.size() < originalSize) {
                        prep.sourcePath = target;
                        prep.size = compressed.size();
                        prep.mime = alpha ? QStringLiteral("image/png")
                                          : QStringLiteral("image/jpeg");
                        prep.sourceIsTemporary = true;
                    } else {
                        QFile::remove(target);
                    }
                }
            }
        }

        auto hash = MediaCrypto::hashFile(prep.sourcePath);
        if (hash.isError()) {
            return Result<MediaPreparation>::error(hash.error());
        }
        prep.fileHash = hash.value();
        return Result<MediaPreparation>::ok(prep);
    });

    auto *watcher = new QFutureWatcher<Result<MediaPreparation>>(this);
    connect(watcher, &QFutureWatcher<Result<MediaPreparation>>::finished, this,
            [watcher, done = std::move(done), self]() mutable {
                const Result<MediaPreparation> result = watcher->result();
                watcher->deleteLater();
                if (result.isError()) {
                    done(result);
                    return;
                }
                if (self) {
                    self->m_cache->ensureDirs();
                }
                done(result);
            });
    watcher->setFuture(future);
}

void MediaService::upload(const QString &chatId, const MediaPreparation &prep,
                          UploadDone done)
{
    if (!m_storage || !m_transport) {
        done(Result<QString>::error(QStringLiteral("media_unavailable")));
        return;
    }
    if (chatId.isEmpty() || prep.size <= 0) {
        done(Result<QString>::error(QStringLiteral("invalid_media")));
        return;
    }
    const QString fileHash = prep.fileHash;
    auto future = m_storage->submit([chatId, fileHash](storage::Database &db) {
        return storage::MediaRepo(db).findPending(chatId, fileHash).value();
    });
    watch<std::optional<MediaRecord>>(this, future,
                                      [this, prep, done](std::optional<MediaRecord> pending) {
        if (pending.has_value() && pending->mediaId.startsWith(QLatin1String("up_"))) {
            startUpload(*pending, prep, done);
            return;
        }
        const qint64 total = MediaCrypto::cipherSize(prep.size);
        m_transport->initUpload(
            total, MediaCrypto::kChunkCipher,
            [this, prep, done](const net::ApiResult<net::MediaUploadSession> &result) {
                if (result.isError()) {
                    const QString code = result.error().code.isEmpty()
                        ? QStringLiteral("media_init_failed")
                        : result.error().code;
                    emit errorOccurred(code, result.error().message);
                    done(Result<QString>::error(code));
                    return;
                }
                MediaRecord record;
                record.mediaId = result.value().uploadId;
                record.kind = prep.kind;
                record.mime = prep.mime;
                record.name = prep.name;
                record.size = prep.size;
                record.chunkSize = MediaCrypto::kChunkPlain;
                record.fileHash = prep.fileHash;
                record.fileKey = prep.fileKey;
                record.nonceBase = prep.nonceBase;
                record.width = prep.width;
                record.height = prep.height;
                record.durationMs = prep.durationMs;
                record.state = QStringLiteral("uploading");
                record.uploadedChunks = 0;
                record.createdAt = QDateTime::currentMSecsSinceEpoch();
                record.updatedAt = record.createdAt;

                if (!m_cache->root().isEmpty()) {
                    m_cache->store(MediaCache::keyFor(record.mediaId, kThumbSide),
                                   prep.thumb320, prep.thumbExtension, true);
                }
                auto stored = m_storage->submit([record, prep](storage::Database &db) {
                    storage::MediaRepo repo(db);
                    repo.insert(record);
                    if (!prep.thumb320.isEmpty()) {
                        repo.setMeta(record.mediaId, QStringLiteral("thumb_b64"),
                                     QString::fromLatin1(prep.thumb320.toBase64()));
                    }
                    if (!prep.preview1280.isEmpty()) {
                        repo.setMeta(record.mediaId, QStringLiteral("preview_b64"),
                                     QString::fromLatin1(prep.preview1280.toBase64()));
                    }
                    return true;
                });
                watch<bool>(this, stored, [this, record, prep, done](bool) {
                    emit thumbnailReady(record.mediaId);
                    startUpload(record, prep, done);
                });
            });
    });
}

void MediaService::startUpload(const MediaRecord &record, const MediaPreparation &prep,
                               UploadDone done)
{
    m_cancelled.remove(record.mediaId);
    m_activeUploads.insert(record.mediaId);
    m_uploadPercents.insert(record.mediaId, 0);
    emit uploadStarted(record.mediaId);
    uploadChunk(record, prep, record.uploadedChunks, 0, std::move(done));
}

void MediaService::uploadChunk(const MediaRecord &record, const MediaPreparation &prep,
                               int index, int attempt, UploadDone done)
{
    const int count = MediaCrypto::chunkCount(prep.size);
    if (index >= count) {
        finishUpload(record, prep, std::move(done));
        return;
    }
    if (m_cancelled.contains(record.mediaId)) {
        m_activeUploads.remove(record.mediaId);
        m_cancelled.remove(record.mediaId);
        done(Result<QString>::error(QStringLiteral("cancelled")));
        return;
    }

    QFile file(prep.sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_activeUploads.remove(record.mediaId);
        done(Result<QString>::error(QStringLiteral("file_unreadable")));
        return;
    }
    const qint64 offset = static_cast<qint64>(index) * MediaCrypto::kChunkPlain;
    if (!file.seek(offset)) {
        done(Result<QString>::error(QStringLiteral("file_seek_failed")));
        return;
    }
    const QByteArray plain =
        file.read(std::min<qint64>(MediaCrypto::kChunkPlain, prep.size - offset));
    const qint64 expected =
        std::min<qint64>(MediaCrypto::kChunkPlain, prep.size - offset);
    if (plain.size() != expected) {
        done(Result<QString>::error(QStringLiteral("file_read_failed")));
        return;
    }
    auto cipher = MediaCrypto::encryptChunk(prep.fileKey, prep.nonceBase, record.mediaId,
                                            index, plain);
    if (cipher.isError()) {
        done(Result<QString>::error(cipher.error()));
        return;
    }

    auto self = QPointer<MediaService>(this);
    m_transport->putChunk(
        record.mediaId, index, cipher.value(),
        [this, self, record, prep, index, attempt, offset, plainSize = plain.size(),
         done = std::move(done)](const net::ApiResult<net::MediaChunkResult> &result) mutable {
            if (result.isError()) {
                if (result.error().isRetryable() && attempt < m_retryDelays.size()) {
                    const int delay = m_retryDelays.at(attempt);
                    QTimer::singleShot(delay, this, [this, record, prep, index, attempt,
                                                     done = std::move(done)]() mutable {
                        uploadChunk(record, prep, index, attempt + 1, std::move(done));
                    });
                    return;
                }
                if (self && self->m_storage) {
                    auto failed = self->m_storage->submit(
                        [mediaId = record.mediaId](storage::Database &db) {
                            return storage::MediaRepo(db)
                                .updateState(mediaId, QStringLiteral("failed"))
                                .isOk();
                        });
                    Q_UNUSED(failed);
                }
                const QString code = result.error().code.isEmpty()
                    ? QStringLiteral("media_upload_failed")
                    : result.error().code;
                if (self && self->m_storage
                    && (code == QLatin1String("media_expired")
                        || code == QLatin1String("media_not_found"))) {
                    auto dropped = self->m_storage->submit(
                        [mediaId = record.mediaId](storage::Database &db) {
                            return storage::MediaRepo(db).remove(mediaId).isOk();
                        });
                    Q_UNUSED(dropped);
                }
                m_activeUploads.remove(record.mediaId);
                emit uploadFinished(record.mediaId, false, code);
                emit errorOccurred(code, result.error().message);
                done(Result<QString>::error(code));
                return;
            }

            MediaRecord updated = record;
            updated.uploadedChunks = index + 1;
            updated.state = QStringLiteral("uploading");
            if (self && self->m_storage) {
                auto stored = self->m_storage->submit(
                    [updated](storage::Database &db) {
                        return storage::MediaRepo(db)
                            .updateState(updated.mediaId, updated.state,
                                         updated.uploadedChunks, -1)
                            .isOk();
                    });
                Q_UNUSED(stored);
            }
            const qint64 sent = offset + plainSize;
            const int percent = prep.size > 0
                ? static_cast<int>((sent * 100) / prep.size)
                : 100;
            m_uploadPercents.insert(record.mediaId, percent);
            emit uploadProgress(record.mediaId, sent, prep.size);
            uploadChunk(updated, prep, index + 1, 0, std::move(done));
        });
}

void MediaService::finishUpload(const MediaRecord &record, const MediaPreparation &prep,
                                UploadDone done)
{
    // The recompressed upload copy is no longer needed once every chunk left.
    if (prep.sourceIsTemporary) {
        QFile::remove(prep.sourcePath);
    }
    const QString mediaId = record.mediaId;
    m_transport->complete(
        mediaId, [this, mediaId, done = std::move(done)](
                     const net::ApiResult<net::MediaCompleteResult> &result) mutable {
            if (result.isError()) {
                const QString code = result.error().code.isEmpty()
                    ? QStringLiteral("media_complete_failed")
                    : result.error().code;
                m_activeUploads.remove(mediaId);
                emit uploadFinished(mediaId, false, code);
                emit errorOccurred(code, result.error().message);
                done(Result<QString>::error(code));
                return;
            }
            if (m_storage) {
                auto stored = m_storage->submit([mediaId](storage::Database &db) {
                    return storage::MediaRepo(db)
                        .updateState(mediaId, QStringLiteral("uploaded"), -1, -1)
                        .isOk();
                });
                Q_UNUSED(stored);
            }
            m_activeUploads.remove(mediaId);
            m_uploadPercents.insert(mediaId, 100);
            emit uploadFinished(mediaId, true, QString());
            done(Result<QString>::ok(mediaId));
        });
}

void MediaService::download(const QString &mediaId, DownloadDone done)
{
    if (!m_storage || !m_transport) {
        done(Result<QString>::error(QStringLiteral("media_unavailable")));
        return;
    }
    auto future = m_storage->submit([mediaId](storage::Database &db) {
        return storage::MediaRepo(db).get(mediaId).value();
    });
    watch<std::optional<MediaRecord>>(this, future,
                                      [this, mediaId, done](std::optional<MediaRecord> record) {
        if (!record.has_value()) {
            done(Result<QString>::error(QStringLiteral("media_not_found")));
            return;
        }
        if (!record->localPath.isEmpty()) {
            const QFileInfo local(record->localPath);
            if (local.exists() && local.size() == record->size) {
                done(Result<QString>::ok(record->localPath));
                return;
            }
        }
        if (record->fileKey.size() != 32 || record->nonceBase.size() != 16) {
            done(Result<QString>::error(QStringLiteral("media_key_invalid")));
            return;
        }
        startDownload(*record, std::move(done));
    });
}

void MediaService::startDownload(const MediaRecord &record, DownloadDone done)
{
    const QString destination = QDir(m_cache->cacheDir())
                                    .filePath(record.mediaId + QStringLiteral(".bin"));
    QDir().mkpath(m_cache->cacheDir());

    const int count = MediaCrypto::chunkCount(record.size);
    int startIndex = qBound(0, record.downloadedChunks, count);
    const qint64 partial = std::min<qint64>(
        record.size, static_cast<qint64>(startIndex) * MediaCrypto::kChunkPlain);
    const QFileInfo existing(destination);
    if (!existing.exists() || existing.size() != partial) {
        QFile::remove(destination);
        startIndex = 0;
    }

    m_cancelled.remove(record.mediaId);
    m_activeDownloads.insert(record.mediaId);
    emit downloadProgress(record.mediaId, partial, record.size);
    downloadChunk(record, startIndex, 0, std::move(done));
}

void MediaService::downloadChunk(const MediaRecord &record, int index, int attempt,
                                 DownloadDone done)
{
    const int count = MediaCrypto::chunkCount(record.size);
    const QString destination = QDir(m_cache->cacheDir())
                                    .filePath(record.mediaId + QStringLiteral(".bin"));
    if (index >= count) {
        auto hash = MediaCrypto::hashFile(destination);
        if (hash.isError() || hash.value() != record.fileHash) {
            QFile::remove(destination);
            if (m_storage) {
                auto failed = m_storage->submit([mediaId = record.mediaId](
                                                    storage::Database &db) {
                    return storage::MediaRepo(db)
                        .updateState(mediaId, QStringLiteral("failed"), -1, 0)
                        .isOk();
                });
                Q_UNUSED(failed);
            }
            m_activeDownloads.remove(record.mediaId);
            emit downloadFinished(record.mediaId, QString(), false,
                                  QStringLiteral("media_hash_mismatch"));
            done(Result<QString>::error(QStringLiteral("media_hash_mismatch")));
            return;
        }
        if (m_storage) {
            auto ready = m_storage->submit([record, destination](storage::Database &db) {
                return storage::MediaRepo(db)
                    .updateState(record.mediaId, QStringLiteral("ready"), -1, -1,
                                 destination)
                    .isOk();
            });
            Q_UNUSED(ready);
        }
        m_activeDownloads.remove(record.mediaId);
        emit downloadProgress(record.mediaId, record.size, record.size);
        emit downloadFinished(record.mediaId, destination, true, QString());
        done(Result<QString>::ok(destination));
        return;
    }
    if (m_cancelled.contains(record.mediaId)) {
        m_activeDownloads.remove(record.mediaId);
        m_cancelled.remove(record.mediaId);
        done(Result<QString>::error(QStringLiteral("cancelled")));
        return;
    }

    auto self = QPointer<MediaService>(this);
    m_transport->getChunk(
        record.mediaId, index,
        [this, self, record, index, attempt, destination,
         done = std::move(done)](const net::ApiResult<QByteArray> &result) mutable {
            if (result.isError()) {
                if (result.error().isRetryable() && attempt < m_retryDelays.size()) {
                    const int delay = m_retryDelays.at(attempt);
                    QTimer::singleShot(delay, this, [this, record, index, attempt,
                                                     done = std::move(done)]() mutable {
                        downloadChunk(record, index, attempt + 1, std::move(done));
                    });
                    return;
                }
                m_activeDownloads.remove(record.mediaId);
                const QString code = result.error().code.isEmpty()
                    ? QStringLiteral("media_download_failed")
                    : result.error().code;
                emit downloadFinished(record.mediaId, QString(), false, code);
                emit errorOccurred(code, result.error().message);
                done(Result<QString>::error(code));
                return;
            }
            auto plain = MediaCrypto::decryptChunk(record.fileKey, record.nonceBase,
                                                   record.mediaId, index, result.value());
            if (plain.isError()) {
                m_activeDownloads.remove(record.mediaId);
                emit downloadFinished(record.mediaId, QString(), false, plain.error());
                done(Result<QString>::error(plain.error()));
                return;
            }
            QFile file(destination);
            if (!file.open(index == 0 ? QIODevice::WriteOnly
                                      : (QIODevice::WriteOnly | QIODevice::Append))) {
                m_activeDownloads.remove(record.mediaId);
                done(Result<QString>::error(QStringLiteral("cache_unwritable")));
                return;
            }
            if (file.write(plain.value()) != plain.value().size()) {
                m_activeDownloads.remove(record.mediaId);
                done(Result<QString>::error(QStringLiteral("cache_write_failed")));
                return;
            }
            file.close();

            MediaRecord updated = record;
            updated.downloadedChunks = index + 1;
            if (self && self->m_storage) {
                auto stored = self->m_storage->submit(
                    [updated](storage::Database &db) {
                        return storage::MediaRepo(db)
                            .updateState(updated.mediaId, QStringLiteral("downloading"),
                                         -1, updated.downloadedChunks)
                            .isOk();
                    });
                Q_UNUSED(stored);
            }
            const qint64 received = std::min<qint64>(
                record.size,
                static_cast<qint64>(index + 1) * MediaCrypto::kChunkPlain);
            emit downloadProgress(record.mediaId, received, record.size);
            downloadChunk(updated, index + 1, 0, std::move(done));
        });
}

void MediaService::cancel(const QString &mediaId)
{
    m_cancelled.insert(mediaId);
}

QPixmap MediaService::thumbnail(const QString &mediaId, int size)
{
    const QString key = MediaCache::keyFor(mediaId, size);
    for (const QString &extension : {QStringLiteral("webp"), QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"), QStringLiteral("png")}) {
        const QByteArray bytes = m_cache->load(key, extension, true);
        if (bytes.isEmpty()) {
            continue;
        }
        QPixmap pixmap;
        if (pixmap.loadFromData(bytes)) {
            return pixmap;
        }
    }
    ensureThumbnail(mediaId, size);
    return {};
}

QPixmap MediaService::preview(const QString &mediaId, int size)
{
    QPixmap pixmap = thumbnail(mediaId, size);
    if (!pixmap.isNull()) {
        return pixmap;
    }
    return thumbnail(mediaId, kThumbSide);
}

QString MediaService::cachedFile(const QString &mediaId) const
{
    const QString path =
        QDir(m_cache->cacheDir()).filePath(mediaId + QStringLiteral(".bin"));
    return QFileInfo::exists(path) ? path : QString();
}

int MediaService::uploadPercent(const QString &mediaId) const
{
    return m_uploadPercents.value(mediaId, -1);
}

void MediaService::registerIncomingThumb(const QString &mediaId, const QByteArray &thumb)
{
    if (thumb.isEmpty() || m_cache->root().isEmpty()) {
        return;
    }
    m_cache->store(MediaCache::keyFor(mediaId, kThumbSide), thumb, QStringLiteral("jpg"),
                   true);
    emit thumbnailReady(mediaId);
}

void MediaService::ensureThumbnail(const QString &mediaId, int size)
{
    const QString key = MediaCache::keyFor(mediaId, size);
    if (m_loadingThumbs.contains(key) || !m_storage) {
        return;
    }
    m_loadingThumbs.insert(key);
    auto future = m_storage->submit([mediaId, size](storage::Database &db) {
        storage::MediaRepo repo(db);
        const QString metaKey =
            size <= kThumbSide ? QStringLiteral("thumb_b64") : QStringLiteral("preview_b64");
        auto meta = repo.meta(mediaId, metaKey);
        return meta.isOk() ? meta.value() : QString();
    });
    watch<QString>(const_cast<MediaService *>(this), future,
                   [this, mediaId, key](QString encoded) {
                       m_loadingThumbs.remove(key);
                       if (encoded.isEmpty() || m_cache->root().isEmpty()) {
                           return;
                       }
                       const QByteArray bytes =
                           QByteArray::fromBase64(encoded.toLatin1());
                       m_cache->store(key, bytes, QStringLiteral("jpg"), true);
                       emit thumbnailReady(mediaId);
                   });
}

} // namespace mini::services
