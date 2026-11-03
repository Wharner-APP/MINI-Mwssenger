#pragma once

#include "QPixmap"
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

namespace mini::services {

struct GifItem
{
    QString id;
    QString title;
    QString category;
    QString file;
};

// §40.8: the bundled animated catalog. Frames are decoded once with
// QImageReader and a single timer drives every visible GIF, so the chat list
// stays cheap even with several animations on screen.
class GifCatalog : public QObject
{
    Q_OBJECT

public:
    static GifCatalog &instance();

    void reload();
    [[nodiscard]] QVector<GifItem> items() const { return m_items; }
    [[nodiscard]] QStringList categories() const;
    [[nodiscard]] bool contains(const QString &id) const;
    [[nodiscard]] QString title(const QString &id) const;
    [[nodiscard]] QString filePath(const QString &id) const;

    [[nodiscard]] int frameCount(const QString &id) const;
    [[nodiscard]] QPixmap frame(const QString &id, int index, const QSize &size) const;
    [[nodiscard]] QPixmap cover(const QString &id, int size) const;

    [[nodiscard]] int currentFrame() const { return m_frame; }
    void start();
    void stop();

signals:
    void frameChanged(int frame);

private:
    explicit GifCatalog(QObject *parent = nullptr);
    void ensureFrames(const QString &id) const;

    QVector<GifItem> m_items;
    mutable QHash<QString, QVector<QPixmap>> m_frames;
    QTimer m_timer;
    int m_frame = 0;
};

} // namespace mini::services
