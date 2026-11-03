#include "ui/widgets/Icons.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace mini::Icons {

namespace {

QByteArray loadSvg(const QString &name)
{
    QFile file(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

QPixmap pixmap(const QString &name, const QColor &color, int size, qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(name, color.name(QColor::HexArgb))
                            .arg(size)
                            .arg(dpr);
    if (const auto it = cache.constFind(key); it != cache.cend()) {
        return it.value();
    }

    QByteArray svg = loadSvg(name);
    if (svg.isEmpty()) {
        return {};
    }
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(qRound(size * dpr), qRound(size * dpr));
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();

    cache.insert(key, pixmap);
    return pixmap;
}

QIcon icon(const QString &name, const QColor &color, int size)
{
    QIcon result;
    result.addPixmap(pixmap(name, color, size, 1.0), QIcon::Normal);
    result.addPixmap(pixmap(name, color, size, 2.0), QIcon::Normal);
    return result;
}

QStringList available()
{
    QStringList result;
    const QStringList files = QDir(QStringLiteral(":/icons")).entryList(
        {QStringLiteral("*.svg")}, QDir::Files);
    for (const QString &file : files) {
        result.append(file.left(file.size() - 4));
    }
    return result;
}

} // namespace mini::Icons
