#include "ui/widgets/ColorSwatch.h"

#include "ui/Theme.h"

#include <QPainter>

namespace mini {

ColorSwatch::ColorSwatch(const QColor &color, QWidget *parent)
    : QAbstractButton(parent), m_color(color)
{
    setCheckable(true);
    setFixedSize(32, 32);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize ColorSwatch::sizeHint() const
{
    return {32, 32};
}

void ColorSwatch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Tokens t = Theme::instance().tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (isChecked()) {
        painter.setPen(QPen(t.textPrimary, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }
    if (hasFocus()) {
        painter.setPen(QPen(t.accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect().adjusted(3, 3, -3, -3));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawEllipse(rect().adjusted(8, 8, -8, -8));
}

} // namespace mini
