#include "ui/widgets/Avatar.h"

#include "ui/Theme.h"

#include <QPainter>

namespace mini {

Avatar::Avatar(QWidget *parent) : QWidget(parent)
{
    setFixedSize(40, 40);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void Avatar::setInitials(const QString &initials)
{
    m_initials = initials.trimmed().left(2).toUpper();
    update();
}

void Avatar::setColor(const QColor &color)
{
    m_color = color;
    update();
}

QSize Avatar::sizeHint() const
{
    return {40, 40};
}

void Avatar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Tokens t = Theme::instance().tokens();
    const QColor fill = m_color.isValid() ? m_color : t.accent;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(rect().adjusted(1, 1, -1, -1));

    painter.setPen(Theme::contrastRatio(fill, t.accentText) >= 4.5 ? t.accentText
                                                                  : t.textPrimary);
    QFont font = Theme::instance().displayFont(14, QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter, m_initials);
}

} // namespace mini
