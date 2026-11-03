#pragma once

#include "core/Result.h"
#include "domain/Media.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

class MediaCache;
class MediaTransport;

// Everything prepareFile() learned about a file before upload. The source
// path may point at a temporary recompressed copy (§40.3 step 4).
struct MediaPreparation
{
    QString sourcePath;
    QString name;
    MediaKind kind = MediaKind::File;
    QString mime;
    qint64 size = 0;
    QString fileHash;
    QByteArray fileKey;
    QByteArray nonceBase;
    QByteArray thumb320;
    QByteArray preview1280;
    QString thumbExtension = QStringLiteral("jpg");
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;
    QString warning;
    bool sourceIsTemporary = false;
};

// §18.4 / §29.11: prepare, encrypt, stage and fetch media. Network work is
// delegated to MediaTransport; the file work runs on the thread pool.
class MediaService : public QObject
{
    Q_OBJECT

public:
    using PrepareDone = std::function<void(Result<MediaPreparation>)>;
    using UploadDone = std::function<void(Result<QString>)>;
    using DownloadDone = std::function<void(Result<QString>)>;

    explicit MediaService(QObject *parent = nullptr);
    ~MediaService() override;

    void setStorage(storage::StorageWorker *storage);
    void setTransport(MediaTransport *transport);
    void setCacheRoot(const QString &root);
    void setPremium(bool premium);
    void setRetryDelays(const QVector<int> &milliseconds);

    [[nodiscard]] MediaCache *cache() const { return m_cache; }
    [[nodiscard]] bool isPremium() const { return m_premium; }

    // asFile skips the §40.3 recompression and keeps the message kind File.
    void prepareFile(const QString &path, PrepareDone done, bool asFile = false);
    void upload(const QString &chatId, const MediaPreparation &prep, UploadDone done);
    void download(const QString &mediaId, DownloadDone done);
    void cancel(const QString &mediaId);

    // Bubble accessors. Never blocks: a missing thumbnail starts one async
    // lookup and returns an empty pixmap until thumbnailReady arrives.
    [[nodiscard]] QPixmap thumbnail(const QString &mediaId, int size = 320);
    [[nodiscard]] QPixmap preview(const QString &mediaId, int size = 1280);
    [[nodiscard]] QString cachedFile(const QString &mediaId) const;
    [[nodiscard]] int uploadPercent(const QString &mediaId) const;
    void registerIncomingThumb(const QString &mediaId, const QByteArray &thumb);

signals:
    // The server assigned the upload id: the UI can now attach progress and
    // the thumbnail to the placeholder bubble.
    void uploadStarted(const QString &mediaId);
    void uploadProgress(const QString &mediaId, qint64 sent, qint64 total);
    void uploadFinished(const QString &mediaId, bool ok, const QString &error);
    void downloadProgress(const QString &mediaId, qint64 received, qint64 total);
    void downloadFinished(const QString &mediaId, const QString &path, bool ok,
                          const QString &error);
    void thumbnailReady(const QString &mediaId);
    void errorOccurred(const QString &code, const QString &detail);

private:
    void startUpload(const MediaRecord &record, const MediaPreparation &prep, UploadDone done);
    void uploadChunk(const MediaRecord &record, const MediaPreparation &prep, int index,
                     int attempt, UploadDone done);
    void finishUpload(const MediaRecord &record, const MediaPreparation &prep,
                      UploadDone done);

    void startDownload(const MediaRecord &record, DownloadDone done);
    void downloadChunk(const MediaRecord &record, int index, int attempt,
                       DownloadDone done);
    void ensureThumbnail(const QString &mediaId, int size);

    storage::StorageWorker *m_storage = nullptr;
    MediaTransport *m_transport = nullptr;
    MediaCache *m_cache = nullptr;
    bool m_premium = false;
    QVector<int> m_retryDelays{1000, 2000, 4000};
    QSet<QString> m_cancelled;
    QSet<QString> m_activeUploads;
    QSet<QString> m_activeDownloads;
    QHash<QString, int> m_uploadPercents;
    mutable QSet<QString> m_loadingThumbs;
};

} // namespace mini::services
