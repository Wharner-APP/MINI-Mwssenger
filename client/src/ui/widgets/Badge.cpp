#include "ui/widgets/Badge.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPainter>

namespace mini {

Badge::Badge(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(18);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void Badge::setCount(int count)
{
    m_count = qMax(0, count);
    updateGeometry();
    update();
}

QSize Badge::sizeHint() const
{
    const Tokens t = Theme::instance().tokens();
    const QString text = m_count > 99 ? QStringLiteral("99+") : QString::number(m_count);
    const QFontMetrics metrics(Theme::instance().appFont());
    return {qMax(18, metrics.horizontalAdvance(text) + 8), 18};
}

void Badge::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Tokens t = Theme::instance().tokens();
    const QString text = m_count > 99 ? QStringLiteral("99+") : QString::number(m_count);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.accent);
    painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);

    painter.setPen(t.accentText);
    painter.setFont(Theme::instance().displayFont(11, QFont::DemiBold));
    painter.drawText(rect(), Qt::AlignCenter, text);
}

} // namespace mini
