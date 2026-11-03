#include "ui/widgets/Toggle.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPropertyAnimation>

namespace mini {

Toggle::Toggle(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 24);
    m_animation = new QPropertyAnimation(this, "position", this);
    m_animation->setDuration(140);
    m_animation->setEasingCurve(QEasingCurve::InOutCubic);
}

void Toggle::setPosition(qreal position)
{
    m_position = qBound<qreal>(0.0, position, 1.0);
    update();
}

QSize Toggle::sizeHint() const
{
    return {44, 24};
}

void Toggle::checkStateSet()
{
    m_animation->stop();
    m_animation->setStartValue(m_position);
    m_animation->setEndValue(isChecked() ? 1.0 : 0.0);
    m_animation->start();
}

void Toggle::nextCheckState()
{
    QAbstractButton::nextCheckState();
}

void Toggle::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Tokens t = Theme::instance().tokens();
    const QRectF track = rect().adjusted(1, 1, -1, -1);
    const QColor trackColor = isChecked() ? t.accent : t.bgInput;
    const QColor knobColor = isChecked() ? t.accentText : t.textSecondary;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(hasFocus() ? QPen(t.accent, 2) : QPen(t.border, 1));
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

    const qreal knobSize = track.height() - 6;
    const qreal left = track.left() + 3 + m_position * (track.width() - knobSize - 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(knobColor);
    painter.drawEllipse(QRectF(left, track.top() + 3, knobSize, knobSize));
}

} // namespace mini
