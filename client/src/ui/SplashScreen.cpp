#include "ui/SplashScreen.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>

namespace mini {

SplashScreen::SplashScreen(QWidget *parent)
    : QSplashScreen(createPixmap(), Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint)
{
    Q_UNUSED(parent);
}

QPixmap SplashScreen::createPixmap()
{
    constexpr int width = 480;
    constexpr int height = 300;
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath card;
    card.addRoundedRect(QRectF(0.5, 0.5, width - 1, height - 1), 24, 24);
    painter.setPen(QPen(QColor(QStringLiteral("#2A2E36")), 1));
    painter.setBrush(QColor(QStringLiteral("#141518")));
    painter.drawPath(card);

    const QPixmap mark = Icons::pixmap(QStringLiteral("app"), QColor(), 56);
    painter.drawPixmap(40, 36, mark);

    QFont brand = Theme::instance().displayFont(34, QFont::Bold);
    painter.setFont(brand);
    painter.setPen(QColor(QStringLiteral("#E8E9EC")));
    painter.drawText(QRect(112, 32, 320, 48), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("MINI"));

    QFont sub = Theme::instance().displayFont(18, QFont::Medium);
    painter.setFont(sub);
    painter.setPen(QColor(QStringLiteral("#A0A6AE")));
    painter.drawText(QRect(114, 78, 320, 28), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Messenger"));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#C8F56B")));
    painter.drawRoundedRect(QRectF(40, 224, 44, 4), 2, 2);

    QFont tagline = Theme::instance().appFont();
    tagline.setPixelSize(12);
    painter.setFont(tagline);
    painter.setPen(QColor(QStringLiteral("#7C838D")));
    painter.drawText(QRect(40, 238, 400, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     QCoreApplication::translate("SplashScreen", "Quiet messenger"));

    return pixmap;
}

} // namespace mini
