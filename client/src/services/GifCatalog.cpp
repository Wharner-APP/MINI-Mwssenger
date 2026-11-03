#include "services/GifCatalog.h"

#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mini::services {

namespace {
constexpr int kFrameIntervalMs = 80;
constexpr int kMaxFrames = 240;
} // namespace

GifCatalog::GifCatalog(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(kFrameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        ++m_frame;
        if (m_frame > 1000000) {
            m_frame = 0;
        }
        emit frameChanged(m_frame);
    });
    reload();
}

GifCatalog &GifCatalog::instance()
{
    static GifCatalog catalog;
    return catalog;
}

void GifCatalog::reload()
{
    m_items.clear();
    m_frames.clear();
    QFile file(QStringLiteral(":/gifs/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        GifItem item;
        item.id = object.value(QStringLiteral("id")).toString();
        item.title = object.value(QStringLiteral("title")).toString();
        item.category = object.value(QStringLiteral("category")).toString();
        item.file = object.value(QStringLiteral("file")).toString();
        if (item.id.isEmpty() || item.file.isEmpty()
            || !QFile::exists(QStringLiteral(":/gifs/") + item.file)) {
            continue;
        }
        if (item.title.isEmpty()) {
            item.title = item.id;
        }
        if (item.category.isEmpty()) {
            item.category = tr("GIF");
        }
        m_items.append(item);
    }
}

QStringList GifCatalog::categories() const
{
    QStringList result;
    for (const GifItem &item : m_items) {
        if (!result.contains(item.category)) {
            result.append(item.category);
        }
    }
    return result;
}

bool GifCatalog::contains(const QString &id) const
{
    for (const GifItem &item : m_items) {
        if (item.id == id) {
            return true;
        }
    }
    return false;
}

QString GifCatalog::title(const QString &id) const
{
    for (const GifItem &item : m_items) {
        if (item.id == id) {
            return item.title;
        }
    }
    return id;
}

QString GifCatalog::filePath(const QString &id) const
{
    for (const GifItem &item : m_items) {
        if (item.id == id) {
            return QStringLiteral(":/gifs/") + item.file;
        }
    }
    return {};
}

void GifCatalog::ensureFrames(const QString &id) const
{
    if (m_frames.contains(id)) {
        return;
    }
    QVector<QPixmap> frames;
    const QString path = filePath(id);
    if (!path.isEmpty()) {
        QImageReader reader(path);
        for (int i = 0; i < kMaxFrames; ++i) {
            QImage image = reader.read();
            if (image.isNull()) {
                break;
            }
            frames.append(QPixmap::fromImage(image));
        }
    }
    m_frames.insert(id, frames);
}

int GifCatalog::frameCount(const QString &id) const
{
    ensureFrames(id);
    return m_frames.value(id).size();
}

QPixmap GifCatalog::frame(const QString &id, int index, const QSize &size) const
{
    ensureFrames(id);
    const QVector<QPixmap> frames = m_frames.value(id);
    if (frames.isEmpty()) {
        return {};
    }
    const QPixmap &source = frames.at(((index % frames.size()) + frames.size()) % frames.size());
    return source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap GifCatalog::cover(const QString &id, int size) const
{
    return frame(id, 0, QSize(size, size));
}

void GifCatalog::start()
{
    if (!m_items.isEmpty() && !m_timer.isActive()) {
        m_timer.start();
    }
}

void GifCatalog::stop()
{
    m_timer.stop();
}

} // namespace mini::services
