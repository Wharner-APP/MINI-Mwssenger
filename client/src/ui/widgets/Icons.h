#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QStringList>

namespace mini::Icons {

// Icons are 24x24 line SVGs. currentColor is replaced with the requested
// colour so the same file works in every theme.
[[nodiscard]] QIcon icon(const QString &name, const QColor &color, int size = 24);
[[nodiscard]] QPixmap pixmap(const QString &name, const QColor &color, int size, qreal dpr = 2.0);
[[nodiscard]] QStringList available();

} // namespace mini::Icons
