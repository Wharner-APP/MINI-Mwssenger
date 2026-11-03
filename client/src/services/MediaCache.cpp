#include "services/MediaCache.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QVector>

#include <algorithm>

namespace mini::services {

namespace {

struct Entry
{
    QString path;
    qint64 size = 0;
    qint64 modified = 0;
};

QVector<Entry> scan(const QString &dir)
{
    QVector<Entry> entries;
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        entries.push_back({path, info.size(), info.lastModified().toMSecsSinceEpoch()});
    }
    return entries;
}

qint64 totalSize(const QVector<Entry> &entries)
{
    qint64 total = 0;
    for (const Entry &entry : entries) {
        total += entry.size;
    }
    return total;
}

void evictTo(const QString &dir, qint64 limit)
{
    QVector<Entry> entries = scan(dir);
    qint64 total = totalSize(entries);
    if (total <= limit) {
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        return left.modified < right.modified;
    });
    for (const Entry &entry : entries) {
        if (total <= limit) {
            break;
        }
        if (QFile::remove(entry.path)) {
            total -= entry.size;
        }
    }
}

} // namespace

MediaCache::MediaCache(const QString &root) : m_root(root) {}

void MediaCache::setRoot(const QString &root)
{
    QMutexLocker lock(&m_mutex);
    m_root = root;
}

void MediaCache::setLimits(qint64 thumbsBytes, qint64 cacheBytes)
{
    QMutexLocker lock(&m_mutex);
    m_thumbLimit = qMax<qint64>(0, thumbsBytes);
    m_cacheLimit = qMax<qint64>(0, cacheBytes);
}

QString MediaCache::root() const
{
    QMutexLocker lock(&m_mutex);
    return m_root;
}

QString MediaCache::thumbsDir() const
{
    return QDir(root()).filePath(QStringLiteral("thumbs"));
}

QString MediaCache::cacheDir() const
{
    return QDir(root()).filePath(QStringLiteral("cache"));
}

QString MediaCache::downloadsDir() const
{
    return QDir(root()).filePath(QStringLiteral("downloads"));
}

bool MediaCache::ensureDirs()
{
    return QDir().mkpath(thumbsDir()) && QDir().mkpath(cacheDir())
        && QDir().mkpath(downloadsDir());
}

QString MediaCache::dirFor(bool thumb) const
{
    return thumb ? thumbsDir() : cacheDir();
}

QString MediaCache::keyFor(const QString &mediaId, int size)
{
    return QStringLiteral("%1_%2").arg(mediaId).arg(size);
}

QString MediaCache::pathFor(const QString &key, const QString &extension, bool thumb) const
{
    return QDir(dirFor(thumb)).filePath(key + QLatin1Char('.') + extension);
}

QString MediaCache::existingPath(const QString &key, bool thumb) const
{
    const QString dir = dirFor(thumb);
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString base = QFileInfo(path).completeBaseName();
        if (base == key) {
            return path;
        }
    }
    return {};
}

QString MediaCache::store(const QString &key, const QByteArray &bytes,
                          const QString &extension, bool thumb)
{
    QString dir;
    {
        QMutexLocker lock(&m_mutex);
        if (m_root.isEmpty() || bytes.isEmpty()) {
            return {};
        }
        dir = QDir(m_root).filePath(thumb ? QStringLiteral("thumbs")
                                          : QStringLiteral("cache"));
    }
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(key + QLatin1Char('.') + extension);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) {
        return {};
    }
    enforceLimits();
    return path;
}

QByteArray MediaCache::load(const QString &key, const QString &extension, bool thumb) const
{
    const QString path = pathFor(key, extension, thumb);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    touchFile(path);
    return bytes;
}

void MediaCache::remove(const QString &key, bool thumb)
{
    const QString path = existingPath(key, thumb);
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

void MediaCache::enforceLimits()
{
    QMutexLocker lock(&m_mutex);
    if (m_root.isEmpty()) {
        return;
    }
    const QString thumbs = QDir(m_root).filePath(QStringLiteral("thumbs"));
    const QString cache = QDir(m_root).filePath(QStringLiteral("cache"));
    const qint64 thumbLimit = m_thumbLimit;
    const qint64 cacheLimit = m_cacheLimit;
    lock.unlock();
    evictTo(thumbs, thumbLimit);
    evictTo(cache, cacheLimit);
}

qint64 MediaCache::sizeOnDisk(bool thumb) const
{
    return totalSize(scan(dirFor(thumb)));
}

void MediaCache::touchFile(const QString &path) const
{
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileAccessTime);
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
}

} // namespace mini::services
