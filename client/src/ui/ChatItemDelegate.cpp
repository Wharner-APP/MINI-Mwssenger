#include "ui/ChatItemDelegate.h"

#include "ui/ChatListModel.h"
#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QFontMetrics>
#include <QList>
#include <QPainter>
#include <QStyle>

namespace mini {

namespace {

QFont fontFor(int px, int weight)
{
    QFont font = Theme::instance().appFont();
    font.setPixelSize(Theme::instance().scaledPx(px));
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QColor blendOver(const QColor &foreground, const QColor &background)
{
    const qreal alpha = foreground.alphaF();
    return QColor::fromRgbF(
        foreground.redF() * alpha + background.redF() * (1.0 - alpha),
        foreground.greenF() * alpha + background.greenF() * (1.0 - alpha),
        foreground.blueF() * alpha + background.blueF() * (1.0 - alpha));
}

// Picks the initials colour with the best contrast on the tinted avatar disc,
// preferring >= 4.5:1 (WCAG AA).
QColor readableOn(const QColor &background, const QList<QColor> &candidates)
{
    QColor best = candidates.isEmpty() ? QColor(Qt::black) : candidates.first();
    double bestRatio = 0.0;
    for (const QColor &candidate : candidates) {
        const double ratio = Theme::contrastRatio(candidate, background);
        if (ratio >= 4.5) {
            return candidate;
        }
        if (ratio > bestRatio) {
            bestRatio = ratio;
            best = candidate;
        }
    }
    return best;
}

QColor avatarColor(const QString &title)
{
    const QVector<AccentDef> accents = Theme::accents();
    if (accents.isEmpty()) {
        return Theme::instance().tokens().accent;
    }
    const int index = static_cast<int>(qHash(title) % static_cast<uint>(accents.size()));
    return accents.at(index).color;
}

} // namespace

ChatItemDelegate::ChatItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QColor ChatItemDelegate::initialsColor(const QColor &accent, const QColor &background)
{
    QColor tint = accent;
    tint.setAlpha(52);
    const QColor tinted = blendOver(tint, background);
    return readableOn(tinted, {QColor(Qt::black), QColor(Qt::white)});
}

QSize ChatItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return {320, Theme::instance().rowHeight()};
}

void ChatItemDelegate::paint(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const Tokens t = Theme::instance().tokens();
    const Theme &theme = Theme::instance();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QRect rect = option.rect;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (selected || hovered) {
        QColor background = t.bgSurface;
        if (!selected) {
            background.setAlpha(140);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(rect.adjusted(6, 2, -6, -2), 10, 10);
    }
    // Keyboard focus must be visibly different from a mouse selection.
    if ((option.state & QStyle::State_HasFocus) && selected) {
        painter->setPen(QPen(t.accent, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(4, 1, -4, -1), 11, 11);
    }

    const bool compact = theme.density() == Density::Compact;
    const int avatarSize = compact ? 36 : 40;
    const int avatarLeft = rect.left() + (compact ? 12 : 14);
    const QRect avatarRect(avatarLeft,
                           rect.top() + (rect.height() - avatarSize) / 2,
                           avatarSize,
                           avatarSize);

    const QString title = index.data(ChatListModel::TitleRole).toString();
    const QString initials = index.data(ChatListModel::InitialsRole).toString();
    const bool online = index.data(ChatListModel::OnlineRole).toBool();
    const int unread = index.data(ChatListModel::UnreadRole).toInt();
    const bool muted = index.data(ChatListModel::MutedRole).toBool();
    const bool pinned = index.data(ChatListModel::PinnedRole).toBool();
    const bool isBot = index.data(ChatListModel::BotRole).toBool();
    const QString time = index.data(ChatListModel::TimeRole).toString();
    const QString preview = index.data(ChatListModel::PreviewRole).toString();

    const QColor accent = avatarColor(title);
    QColor avatarFill = accent;
    avatarFill.setAlpha(52);
    painter->setPen(Qt::NoPen);
    painter->setBrush(avatarFill);
    painter->drawEllipse(avatarRect);

    painter->setPen(initialsColor(accent, t.bgList));
    painter->setFont(fontFor(compact ? 12 : 13, QFont::DemiBold));
    painter->drawText(avatarRect, Qt::AlignCenter, initials);

    if (online) {
        const int dot = compact ? 8 : 9;
        const QRect dotRect(avatarRect.right() - dot + 1,
                            avatarRect.bottom() - dot + 1,
                            dot,
                            dot);
        painter->setPen(QPen(t.bgList, 2));
        painter->setBrush(t.online);
        painter->drawEllipse(dotRect);
    }

    const int textLeft = avatarRect.right() + 10;
    const int timeWidth = 46;
    const int textRight = rect.right() - timeWidth - 16;
    const int textWidth = qMax(20, textRight - textLeft);
    const int titleTop = rect.top() + (compact ? 8 : 12);
    const int titleHeight = compact ? 18 : 20;

    if (pinned) {
        const QPixmap pin = Icons::pixmap(QStringLiteral("pin"), t.textTertiary, 12);
        painter->drawPixmap(textLeft, titleTop + 3, pin);
    }
    const int titleOffset = pinned ? 18 : 0;

    painter->setPen(t.textPrimary);
    painter->setFont(fontFor(compact ? 13 : 14, QFont::DemiBold));
    const QFontMetrics titleMetrics(painter->font());
    // §29.14: reserve room for the "BOT" badge so the name never runs into it.
    const int botBadgeWidth = isBot ? 30 : 0;
    const QString elidedTitle =
        titleMetrics.elidedText(title, Qt::ElideRight,
                                qMax(20, textWidth - titleOffset - botBadgeWidth));
    painter->drawText(QRect(textLeft + titleOffset, titleTop, textWidth - titleOffset,
                            titleHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

    if (isBot) {
        const int drawn = qMin(titleMetrics.horizontalAdvance(elidedTitle),
                               textWidth - titleOffset);
        const QRect badge(textLeft + titleOffset + drawn + 5, titleTop + 3, 27, 14);
        QColor tint = t.accent;
        tint.setAlpha(48);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(badge, 7, 7);
        painter->setPen(t.accent);
        painter->setFont(fontFor(8, QFont::DemiBold));
        painter->drawText(badge, Qt::AlignCenter, tr("BOT"));
    }

    if (!compact) {
        painter->setPen(t.textSecondary);
        painter->setFont(fontFor(12, QFont::Normal));
        const QFontMetrics previewMetrics(painter->font());
        const int previewWidth = qMax(20, textWidth - (unread > 0 ? 30 : 0));
        painter->drawText(QRect(textLeft, titleTop + titleHeight + 1, previewWidth, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          previewMetrics.elidedText(preview, Qt::ElideRight, previewWidth));
    }

    painter->setPen(t.textTertiary);
    painter->setFont(fontFor(11, QFont::Normal));
    painter->drawText(QRect(rect.right() - timeWidth - 12, titleTop, timeWidth, 16),
                      Qt::AlignRight | Qt::AlignVCenter,
                      time);

    int rightIconOffset = 0;
    if (muted) {
        const QPixmap bell = Icons::pixmap(QStringLiteral("bell-off"), t.textTertiary, 12);
        painter->drawPixmap(rect.right() - 16 - 12, titleTop + 2, bell);
        rightIconOffset = 16;
    }

    if (unread > 0) {
        const QString count = unread > 99 ? QStringLiteral("99+") : QString::number(unread);
        painter->setFont(fontFor(10, QFont::DemiBold));
        const QFontMetrics badgeMetrics(painter->font());
        const int badgeHeight = 16;
        const int badgeWidth = qMax(18, badgeMetrics.horizontalAdvance(count) + 8);
        const QRect badgeRect(rect.right() - 12 - rightIconOffset - badgeWidth,
                              rect.bottom() - badgeHeight - (compact ? 7 : 12),
                              badgeWidth,
                              badgeHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.accent);
        painter->drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
        painter->setPen(t.accentText);
        painter->drawText(badgeRect, Qt::AlignCenter, count);
    }

    painter->restore();
}

} // namespace mini
