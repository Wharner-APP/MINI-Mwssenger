#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QPainter;
class QRect;

namespace mini::markdown {

// Height of the formatted body when wrapped to `width`, including the quote
// bar inset. Used by both sizeHint and paint so the bubble never jumps.
[[nodiscard]] int layoutHeight(const QString &text, const QFont &font, int width);

// Draws the §35.1 formatting inside `rect`. `highlight` marks every
// case-insensitive occurrence (in-chat search); an empty string disables it.
void paint(QPainter *painter, const QRect &rect, const QString &text,
           const QFont &font, const QColor &textColor, const QColor &secondary,
           const QColor &accent, const QColor &codeBackground,
           const QString &highlight = QString());

// Text with the markup characters removed, for previews and the clipboard.
[[nodiscard]] QString plainText(const QString &input);

} // namespace mini::markdown
