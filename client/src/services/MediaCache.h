#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>

namespace mini::services {

// §40.12: data/media/thumbs (LRU 256 MiB), cache (LRU 2 GiB), downloads
// (user-saved, never evicted). Reads touch the file time, eviction is by the
// oldest last access, so the policy survives a restart without a side index.
class MediaCache
{
public:
    explicit MediaCache(const QString &root = QString());

    void setRoot(const QString &root);
    void setLimits(qint64 thumbsBytes, qint64 cacheBytes);
    [[nodiscard]] QString root() const;
    [[nodiscard]] QString thumbsDir() const;
    [[nodiscard]] QString cacheDir() const;
    [[nodiscard]] QString downloadsDir() const;

    [[nodiscard]] bool ensureDirs();

    // Writes atomically and returns the stored path. Empty on failure.
    QString store(const QString &key, const QByteArray &bytes, const QString &extension,
                  bool thumb);
    [[nodiscard]] QByteArray load(const QString &key, const QString &extension,
                                  bool thumb) const;
    [[nodiscard]] QString pathFor(const QString &key, const QString &extension,
                                  bool thumb) const;
    [[nodiscard]] QString existingPath(const QString &key, bool thumb) const;
    void remove(const QString &key, bool thumb);

    // Sum and evict; safe to call from any thread.
    void enforceLimits();
    [[nodiscard]] qint64 sizeOnDisk(bool thumb) const;

    [[nodiscard]] static QString keyFor(const QString &mediaId, int size);

private:
    [[nodiscard]] QString dirFor(bool thumb) const;
    void touchFile(const QString &path) const;

    mutable QMutex m_mutex;
    QString m_root;
    qint64 m_thumbLimit = 256LL * 1024 * 1024;
    qint64 m_cacheLimit = 2048LL * 1024 * 1024;
};

} // namespace mini::services
