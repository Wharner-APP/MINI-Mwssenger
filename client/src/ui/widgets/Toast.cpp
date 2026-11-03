#include "ui/widgets/Toast.h"

#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace mini {

Toast::Toast(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("notificationToast"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setMaximumWidth(340);
    setMinimumWidth(260);

    const Tokens tokens = Theme::instance().tokens();
    setStyleSheet(QStringLiteral(
                      "QFrame#notificationToast { background: %1; border: 1px solid %2;"
                      "  border-radius: 12px; }"
                      "QLabel { background: transparent; color: %3; }")
                      .arg(tokens.bgSurface.name(), tokens.border.name(),
                           tokens.textPrimary.name()));

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("notificationToastTitle"));
    QFont titleFont = Theme::instance().appFont();
    titleFont.setPixelSize(Theme::instance().scaledPx(13));
    titleFont.setWeight(QFont::DemiBold);
    m_title->setFont(titleFont);

    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("notificationToastBody"));
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    QFont bodyFont = Theme::instance().appFont();
    bodyFont.setPixelSize(Theme::instance().scaledPx(12));
    m_body->setFont(bodyFont);
    m_body->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.textSecondary.name()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(2);
    layout->addWidget(m_title);
    layout->addWidget(m_body);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Toast::dismiss);

    hide();
}

void Toast::showMessage(const QString &title, const QString &body, int timeoutMs)
{
    m_title->setText(title);
    m_body->setText(body);
    m_body->setVisible(!body.isEmpty());
    adjustSize();
    reposition();
    show();
    raise();
    m_timer->start(qMax(1000, timeoutMs));
}

void Toast::dismiss()
{
    m_timer->stop();
    hide();
}

void Toast::reposition()
{
    if (!parentWidget()) {
        return;
    }
    const QRect area = parentWidget()->rect();
    const int margin = 18;
    const QSize hint = sizeHint();
    resize(qMin(hint.width(), area.width() - 2 * margin), hint.height());
    move(area.right() - width() - margin, area.bottom() - height() - margin);
}

void Toast::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    reposition();
}

void Toast::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        dismiss();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

} // namespace mini
