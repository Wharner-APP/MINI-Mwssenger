#include "ui/widgets/Spinner.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QTimer>

namespace mini {

Spinner::Spinner(QWidget *parent) : QWidget(parent)
{
    setFixedSize(24, 24);
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 8) % 360;
        update();
    });
}

void Spinner::start()
{
    if (!m_timer->isActive()) {
        m_timer->start();
    }
}

void Spinner::stop()
{
    m_timer->stop();
}

bool Spinner::isRunning() const
{
    return m_timer->isActive();
}

QSize Spinner::sizeHint() const
{
    return {24, 24};
}

void Spinner::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Tokens t = Theme::instance().tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(t.accent, 2.4, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    const QRectF box = rect().adjusted(3, 3, -3, -3);
    painter.drawArc(box, m_angle * 16, 250 * 16);
}

} // namespace mini
