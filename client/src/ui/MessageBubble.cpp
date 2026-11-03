#include "ui/MessageBubble.h"
#include <QCoreApplication>

#include "services/GifCatalog.h"
#include "services/MediaService.h"
#include "services/StickerService.h"
#include "ui/Markdown.h"
#include "ui/MarkdownPainter.h"
#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVariantMap>

#include <algorithm>

namespace mini {

namespace {

constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kMetaHeight = 16;
constexpr int kReplyHeight = 20;
constexpr int kMediaHeight = 180;
constexpr int kFileCardHeight = 56;
constexpr int kMediaMaxWidth = 280;
constexpr int kStickerSize = 160;
constexpr int kGifWidth = 240;
constexpr int kGifHeight = 144;
constexpr int kAlbumWidth = 280;
constexpr int kAlbumGap = 2;
constexpr int kReactionChipHeight = 22;
constexpr int kReactionGap = 4;
constexpr int kPollWidth = 340;
constexpr int kPollOptionHeight = 34;
constexpr int kPollOptionGap = 4;
constexpr int kPollQuestionLines = 2;

QFont messageFont()
{
    QFont font = Theme::instance().appFont();
    font.setPixelSize(Theme::instance().scaledPx(14));
    return font;
}

QFont metaFont()
{
    QFont font = Theme::instance().appFont();
    font.setPixelSize(Theme::instance().scaledPx(11));
    return font;
}

int bubbleMaxWidth(const QStyleOptionViewItem &option)
{
    const int viewWidth = option.widget ? option.widget->width() : 640;
    return qMax(180, static_cast<int>(viewWidth * 0.72));
}

int albumHeight(int count)
{
    if (count <= 1) {
        return 200;
    }
    if (count == 2) {
        return 140;
    }
    if (count == 3) {
        return 180;
    }
    if (count == 4) {
        return 260;
    }
    const int rows = (count + 2) / 3;
    const int cell = 92;
    return rows * cell + (rows - 1) * kAlbumGap;
}

// §40.10 grid: 1 large; 2 side by side; 3 one large + two; 4 2x2; 5-10 tiled.
QVector<QRect> albumCells(int count, const QRect &area)
{
    QVector<QRect> cells;
    if (count <= 0) {
        return cells;
    }
    const int gap = kAlbumGap;
    if (count == 1) {
        cells.append(area);
        return cells;
    }
    if (count == 2) {
        const int width = (area.width() - gap) / 2;
        cells.append(QRect(area.left(), area.top(), width, area.height()));
        cells.append(QRect(area.left() + width + gap, area.top(),
                           area.width() - width - gap, area.height()));
        return cells;
    }
    if (count == 3) {
        const int big = qMin(area.width() * 2 / 3, area.height());
        const int smallWidth = area.width() - big - gap;
        const int smallHeight = (area.height() - gap) / 2;
        cells.append(QRect(area.left(), area.top(), big, area.height()));
        cells.append(QRect(area.left() + big + gap, area.top(), smallWidth, smallHeight));
        cells.append(QRect(area.left() + big + gap, area.top() + smallHeight + gap,
                           smallWidth, area.height() - smallHeight - gap));
        return cells;
    }
    if (count == 4) {
        const int width = (area.width() - gap) / 2;
        const int height = (area.height() - gap) / 2;
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                cells.append(QRect(area.left() + column * (width + gap),
                                   area.top() + row * (height + gap), width, height));
            }
        }
        return cells;
    }
    const int columns = 3;
    const int rows = (count + columns - 1) / columns;
    const int width = (area.width() - gap * (columns - 1)) / columns;
    const int height = (area.height() - gap * (rows - 1)) / rows;
    for (int i = 0; i < count; ++i) {
        const int row = i / columns;
        const int column = i % columns;
        cells.append(QRect(area.left() + column * (width + gap),
                           area.top() + row * (height + gap), width, height));
    }
    return cells;
}

QString formatTime(qint64 milliseconds)
{
    if (milliseconds <= 0) {
        return {};
    }
    const QDateTime moment = QDateTime::fromMSecsSinceEpoch(milliseconds);
    const QDate today = QDate::currentDate();
    if (moment.date() == today) {
        return moment.toString(QStringLiteral("HH:mm"));
    }
    if (moment.date().year() == today.year()) {
        return moment.toString(QStringLiteral("dd.MM"));
    }
    return moment.toString(QStringLiteral("dd.MM.yy"));
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QCoreApplication::translate("MessageBubble", "%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QCoreApplication::translate("MessageBubble", "%1 KB").arg(bytes / 1024);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QCoreApplication::translate("MessageBubble", "%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    }
    return QCoreApplication::translate("MessageBubble", "%1 GB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds <= 0) {
        return {};
    }
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString statusIcon(MessageState state)
{
    switch (state) {
    case MessageState::Queued:
    case MessageState::Sending:
        return QStringLiteral("clock");
    case MessageState::Sent:
        return QStringLiteral("check");
    case MessageState::Delivered:
    case MessageState::Read:
        return QStringLiteral("checks");
    case MessageState::Failed:
        return QStringLiteral("info");
    case MessageState::Received:
    case MessageState::DeletedLocal:
        return {};
    }
    return {};
}

QColor statusColor(MessageState state, const Tokens &tokens)
{
    switch (state) {
    case MessageState::Read:
        return tokens.accent;
    case MessageState::Failed:
        return tokens.danger;
    case MessageState::Delivered:
        return tokens.textSecondary;
    default:
        return tokens.textTertiary;
    }
}

struct ReactionChipBox
{
    QRect rect;
    QString emoji;
};

// §38.1: chips are laid out left-to-right under the bubble and wrap to a new
// row when they no longer fit. Both paint() and the hit test use this helper.
QVector<ReactionChipBox> reactionChips(const QStyleOptionViewItem &option,
                                       const QModelIndex &index, int left, int top,
                                       int maxWidth)
{
    QVector<ReactionChipBox> chips;
    const QVariantList reactions = index.data(MessageListModel::ReactionsRole).toList();
    if (reactions.isEmpty()) {
        return chips;
    }
    QFont emojiFont = Theme::instance().appFont();
    emojiFont.setPixelSize(Theme::instance().scaledPx(13));
    const QFontMetrics emojiMetrics(emojiFont);
    const QFontMetrics meta(metaFont());
    int x = left;
    int y = top;
    for (const QVariant &value : reactions) {
        const QVariantMap chip = value.toMap();
        const QString emoji = chip.value(QStringLiteral("emoji")).toString();
        const int count = chip.value(QStringLiteral("count")).toInt();
        const int width = emojiMetrics.horizontalAdvance(emoji)
            + meta.horizontalAdvance(QString::number(count)) + 20;
        if (x > left && x + width > left + maxWidth) {
            x = left;
            y += kReactionChipHeight + kReactionGap;
        }
        ReactionChipBox box;
        box.rect = QRect(x, y, width, kReactionChipHeight);
        box.emoji = emoji;
        chips.append(box);
        x += width + kReactionGap;
    }
    return chips;
}

int reactionRowsHeight(const QVector<ReactionChipBox> &chips)
{
    if (chips.isEmpty()) {
        return 0;
    }
    int bottom = 0;
    for (const ReactionChipBox &chip : chips) {
        bottom = qMax(bottom, chip.rect.bottom());
    }
    return bottom - chips.first().rect.top() + 1;
}

void paintReactionChips(QPainter *painter, const QVector<ReactionChipBox> &chips,
                        const QVariantList &reactions, const Tokens &tokens)
{
    QHash<QString, QPair<int, bool>> values;
    for (const QVariant &value : reactions) {
        const QVariantMap chip = value.toMap();
        values.insert(chip.value(QStringLiteral("emoji")).toString(),
                      {chip.value(QStringLiteral("count")).toInt(),
                       chip.value(QStringLiteral("mine")).toBool()});
    }
    QFont emojiFont = Theme::instance().appFont();
    emojiFont.setPixelSize(Theme::instance().scaledPx(13));
    for (const ReactionChipBox &chip : chips) {
        const auto info = values.value(chip.emoji);
        painter->setPen(Qt::NoPen);
        if (info.second) {
            painter->setBrush(QColor(tokens.accent.red(), tokens.accent.green(),
                                     tokens.accent.blue(), 55));
        } else {
            painter->setBrush(tokens.bgInput);
        }
        painter->drawRoundedRect(chip.rect, 11, 11);
        if (info.second) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(tokens.accent, 1));
            painter->drawRoundedRect(chip.rect.adjusted(1, 1, -1, -1), 10, 10);
        }
        painter->setPen(tokens.textPrimary);
        painter->setFont(emojiFont);
        painter->drawText(chip.rect.adjusted(8, 0, -6, 0),
                          Qt::AlignLeft | Qt::AlignVCenter, chip.emoji);
        painter->setFont(metaFont());
        painter->drawText(chip.rect.adjusted(0, 0, -7, 0),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(info.first));
    }
}

struct BubbleLayout
{
    int width = 0;
    int height = 0;
    int mediaHeight = 0;
    int captionHeight = 0;
    int forwardHeight = 0;
    int reactionHeight = 0;
    int pollOptions = 0;
    bool hasMedia = false;
    bool hasReactions = false;
    bool isPoll = false;
    bool isSystem = false;
    MediaKind kind = MediaKind::Text;
};

BubbleLayout measureBubble(const QStyleOptionViewItem &option, const QModelIndex &index)
{
    BubbleLayout layout;
    layout.kind = static_cast<MediaKind>(index.data(MessageListModel::KindRole).toInt());
    layout.hasMedia = layout.kind != MediaKind::Text;
    layout.isSystem = index.data(MessageListModel::SystemRole).toBool();
    const QString pollId = index.data(MessageListModel::PollIdRole).toString();
    layout.isPoll = !pollId.isEmpty();
    const QString body = index.data(MessageListModel::BodyRole).toString();
    if (layout.isSystem) {
        // §38.4: a centred grey line without a bubble.
        layout.width = bubbleMaxWidth(option);
        layout.height = 32;
        return layout;
    }
    const QString replyId = index.data(MessageListModel::ReplyRole).toString();
    const QString forwarded = index.data(MessageListModel::ForwardedRole).toString();
    if (!forwarded.isEmpty()) {
        layout.forwardHeight = 16;
    }
    QString reply = index.data(MessageListModel::ReplyTextRole).toString();
    if (reply.isEmpty()) {
        reply = replyId;
    }

    const QFontMetrics metrics(messageFont());
    const int maxBubble = bubbleMaxWidth(option);
    if (layout.isPoll) {
        layout.width = qMin(maxBubble, kPollWidth + kPadX * 2);
    } else if (layout.hasMedia) {
        switch (layout.kind) {
        case MediaKind::Sticker:
            layout.width = qMin(maxBubble, kStickerSize + kPadX * 2);
            break;
        case MediaKind::Gif:
            layout.width = qMin(maxBubble, kGifWidth + kPadX * 2);
            break;
        case MediaKind::Album:
            layout.width = qMin(maxBubble, kAlbumWidth + kPadX * 2);
            break;
        default:
            layout.width = qMin(maxBubble, kMediaMaxWidth);
            break;
        }
    } else {
        const QString previewText = markdown::plainText(body);
        layout.width = qBound(160, metrics.horizontalAdvance(previewText) + kPadX * 2,
                              maxBubble);
    }
    const int textWidth = qMax(40, layout.width - kPadX * 2);
    const bool drawsCaption = layout.kind != MediaKind::Sticker
        && layout.kind != MediaKind::Gif;
    layout.captionHeight = (!drawsCaption || body.isEmpty())
        ? 0
        : markdown::layoutHeight(body, messageFont(), textWidth);
    if (layout.kind == MediaKind::Photo || layout.kind == MediaKind::Video) {
        layout.mediaHeight = kMediaHeight;
    } else if (layout.kind == MediaKind::File) {
        layout.mediaHeight = kFileCardHeight;
    } else if (layout.kind == MediaKind::Sticker) {
        layout.mediaHeight = kStickerSize;
    } else if (layout.kind == MediaKind::Gif) {
        layout.mediaHeight = kGifHeight;
    } else if (layout.kind == MediaKind::Album) {
        layout.mediaHeight = albumHeight(
            index.data(MessageListModel::AlbumIdsRole).toStringList().size());
    } else if (layout.isPoll) {
        layout.pollOptions =
            index.data(MessageListModel::PollOptionsRole).toStringList().size();
        // Option rows plus a footer line with the vote total and Close.
        layout.mediaHeight =
            layout.pollOptions * (kPollOptionHeight + kPollOptionGap) + 22;
    }
    int height = kPadY * 2 + kMetaHeight + layout.mediaHeight + layout.captionHeight
        + layout.forwardHeight;
    if (!forwarded.isEmpty()) {
        height += 2;
    }
    if ((layout.hasMedia || layout.isPoll) && layout.captionHeight > 0) {
        height += 6;
    }
    if (!reply.isEmpty()) {
        height += kReplyHeight;
    }
    // §38.1: chips add their own band below the bubble.
    const int chipWidth = (option.widget ? option.widget->width() : 640) - 24;
    const QVector<ReactionChipBox> chips = reactionChips(option, index, 0, 0, chipWidth);
    layout.hasReactions = !chips.isEmpty();
    layout.reactionHeight = reactionRowsHeight(chips) + (chips.isEmpty() ? 0 : 4);
    height += layout.reactionHeight;
    layout.height = qMax(38, height);
    return layout;
}

struct PollGeometry
{
    QRect bubble;
    QRect question;
    QVector<QRect> options;
    QRect footer;
    QRect close;
};

PollGeometry pollGeometry(const QStyleOptionViewItem &option, const QModelIndex &index,
                          const BubbleLayout &layout)
{
    PollGeometry geometry;
    const bool outgoing = index.data(MessageListModel::OutgoingRole).toBool();
    const int left = outgoing ? option.rect.right() - layout.width - 12
                              : option.rect.left() + 12;
    const int top = option.rect.top() + 3;
    geometry.bubble = QRect(left, top, layout.width,
                            layout.height - 6 - layout.reactionHeight);
    const int width = geometry.bubble.width() - kPadX * 2;
    int y = geometry.bubble.top() + kPadY;
    if (!index.data(MessageListModel::ReplyRole).toString().isEmpty()) {
        y += kReplyHeight;
    }
    const QString forwarded = index.data(MessageListModel::ForwardedRole).toString();
    if (!forwarded.isEmpty()) {
        y += layout.forwardHeight + 2;
    }
    geometry.question = QRect(geometry.bubble.left() + kPadX, y, width,
                              layout.captionHeight + 2);
    y = geometry.question.bottom() + 6;
    const int count =
        index.data(MessageListModel::PollOptionsRole).toStringList().size();
    for (int i = 0; i < count; ++i) {
        geometry.options.append(QRect(geometry.bubble.left() + kPadX, y, width,
                                      kPollOptionHeight));
        y += kPollOptionHeight + kPollOptionGap;
    }
    const int metaTop = geometry.bubble.bottom() - kPadY - kMetaHeight;
    geometry.footer = QRect(geometry.bubble.left() + kPadX, metaTop - 20,
                            width, 18);
    geometry.close = QRect(geometry.footer.right() - 52, geometry.footer.top(),
                           52, geometry.footer.height());
    return geometry;
}

void drawPixmapCover(QPainter *painter, const QRect &target, const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return;
    }
    const QPixmap scaled = pixmap.scaled(target.size(), Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    const QRect source((scaled.width() - target.width()) / 2,
                       (scaled.height() - target.height()) / 2, target.width(),
                       target.height());
    painter->drawPixmap(target, scaled, source);
}

} // namespace

MessageListModel::MessageListModel(QObject *parent) : QAbstractListModel(parent) {}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }
    const Message &message = m_messages.at(index.row());
    switch (role) {
    case ClientMsgIdRole:
        return message.clientMsgId;
    case ChatIdRole:
        return message.chatId;
    case BodyRole:
        return message.body;
    case TimeRole:
        return message.serverTs > 0 ? message.serverTs : message.localTs;
    case OutgoingRole:
        return message.outgoing;
    case StateRole:
        return static_cast<int>(message.state);
    case ReplyRole:
        return message.replyToClientMsgId;
    case ReplyTextRole:
        return m_replyPreviews.value(message.clientMsgId);
    case SenderRole:
        return message.senderLogin;
    case KindRole:
        return static_cast<int>(message.kind);
    case MediaIdRole:
        return message.mediaId;
    case ThumbRole:
        if (!m_media || message.mediaId.isEmpty()) {
            return {};
        }
        if (message.kind == MediaKind::Album) {
            const QStringList ids = splitMediaIds(message.mediaId);
            return ids.isEmpty() ? QVariant() : QVariant(m_media->thumbnail(ids.first(), 320));
        }
        return m_media->thumbnail(message.mediaId, 320);
    case UploadPercentRole:
        return m_uploadPercents.value(message.mediaId, -1);
    case MediaNameRole:
        return message.mediaName;
    case MediaSizeRole:
        return message.mediaSize;
    case DurationRole:
        return message.mediaDurationMs;
    case StickerPackRole:
        return message.kind == MediaKind::Sticker
            ? message.mediaId.section(QLatin1Char('/'), 0, 0)
            : QString();
    case StickerIdRole:
        return message.kind == MediaKind::Sticker
            ? message.mediaId.section(QLatin1Char('/'), 1, 1)
            : QString();
    case StickerPixmapRole:
        if (message.kind == MediaKind::Sticker && m_stickers) {
            return m_stickers->stickerPixmap(
                message.mediaId.section(QLatin1Char('/'), 0, 0),
                message.mediaId.section(QLatin1Char('/'), 1, 1), kStickerSize);
        }
        return {};
    case GifIdRole:
        return message.kind == MediaKind::Gif ? message.mediaId : QString();
    case GifFrameRole:
        if (message.kind == MediaKind::Gif) {
            services::GifCatalog &catalog = services::GifCatalog::instance();
            return catalog.frame(message.mediaId, catalog.currentFrame(),
                                 QSize(kGifWidth, kGifHeight));
        }
        return {};
    case AlbumIdsRole:
        return message.kind == MediaKind::Album ? splitMediaIds(message.mediaId)
                                                : QStringList();
    case AlbumThumbsRole: {
        QVariantList thumbs;
        if (message.kind != MediaKind::Album || !m_media) {
            return thumbs;
        }
        for (const QString &mediaId : splitMediaIds(message.mediaId)) {
            thumbs.append(QVariant::fromValue(m_media->thumbnail(mediaId, 320)));
        }
        return thumbs;
    }
    case EditedRole:
        return message.editedAt > 0;
    case ForwardedRole:
        return message.forwardedFrom;
    case SearchMatchRole:
        return m_searchMatches.contains(message.clientMsgId)
            || message.clientMsgId == m_highlighted;
    case SearchQueryRole:
        return m_searchQuery;
    case ReactionsRole:
        return m_reactions.value(message.clientMsgId);
    case PollIdRole:
        return message.pollId;
    case PollQuestionRole:
        return message.body;
    case PollOptionsRole: {
        const auto it = m_polls.constFind(message.pollId);
        return it == m_polls.constEnd() ? QStringList() : it->poll.options;
    }
    case PollCountsRole: {
        const auto it = m_polls.constFind(message.pollId);
        QVariantList counts;
        if (it != m_polls.constEnd()) {
            for (const int count : it->counts) {
                counts.append(count);
            }
        }
        return counts;
    }
    case PollMyChoicesRole: {
        const auto it = m_polls.constFind(message.pollId);
        QVariantList choices;
        if (it != m_polls.constEnd()) {
            for (const int choice : it->myChoices) {
                choices.append(choice);
            }
        }
        return choices;
    }
    case PollFlagsRole: {
        const auto it = m_polls.constFind(message.pollId);
        int flags = 0;
        if (it != m_polls.constEnd()) {
            flags |= it->poll.multiple ? 0x1 : 0;
            flags |= it->poll.anonymous ? 0x2 : 0;
            flags |= it->poll.quiz ? 0x4 : 0;
            flags |= it->poll.closed ? 0x8 : 0;
            flags |= it->canClose ? 0x10 : 0;
        }
        return flags;
    }
    case PollCorrectRole: {
        const auto it = m_polls.constFind(message.pollId);
        return it == m_polls.constEnd() ? -1 : it->poll.correctOption;
    }
    case PollTotalRole: {
        const auto it = m_polls.constFind(message.pollId);
        return it == m_polls.constEnd() ? 0 : it->totalVoters;
    }
    case SystemRole:
        return message.system;
    case Qt::DisplayRole:
        return message.body;
    case Qt::ToolTipRole:
        return message.body;
    default:
        return {};
    }
}

void MessageListModel::setMessages(const QVector<Message> &messages)
{
    beginResetModel();
    m_messages = messages;
    std::stable_sort(m_messages.begin(), m_messages.end(), [](const Message &left,
                                                             const Message &right) {
        const qint64 leftTs = effectiveTs(left);
        const qint64 rightTs = effectiveTs(right);
        if (leftTs != rightTs) {
            return leftTs < rightTs;
        }
        return left.clientMsgId < right.clientMsgId;
    });
    endResetModel();
    updateGifPlayback();
}

void MessageListModel::appendMessage(const Message &message)
{
    if (indexOf(message.clientMsgId) >= 0) {
        updateMessage(message);
        return;
    }
    insertSorted(message);
    updateGifPlayback();
}

void MessageListModel::updateMessage(const Message &message)
{
    const int row = indexOf(message.clientMsgId);
    if (row < 0) {
        insertSorted(message);
        return;
    }
    m_messages[row] = message;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed);
    updateGifPlayback();
}

void MessageListModel::removeMessage(const QString &clientMsgId)
{
    const int row = indexOf(clientMsgId);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_messages.removeAt(row);
    endRemoveRows();
    updateGifPlayback();
}

Message MessageListModel::messageAt(int row) const
{
    if (row < 0 || row >= m_messages.size()) {
        return {};
    }
    return m_messages.at(row);
}

int MessageListModel::indexOf(const QString &clientMsgId) const
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).clientMsgId == clientMsgId) {
            return row;
        }
    }
    return -1;
}

void MessageListModel::setReplyPreview(const QString &clientMsgId, const QString &text)
{
    m_replyPreviews.insert(clientMsgId, text);
    const int row = indexOf(clientMsgId);
    if (row >= 0) {
        const QModelIndex changed = index(row, 0);
        emit dataChanged(changed, changed);
    }
}

void MessageListModel::setMediaService(services::MediaService *media)
{
    m_media = media;
    if (!m_messages.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0));
    }
}

void MessageListModel::setStickerService(services::StickerService *stickers)
{
    m_stickers = stickers;
    if (!m_messages.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0));
    }
}

void MessageListModel::updateGifPlayback()
{
    bool hasGif = false;
    for (const Message &message : m_messages) {
        if (message.kind == MediaKind::Gif) {
            hasGif = true;
            break;
        }
    }
    services::GifCatalog &catalog = services::GifCatalog::instance();
    if (!m_gifConnected) {
        m_gifConnected = true;
        connect(&catalog, &services::GifCatalog::frameChanged, this,
                &MessageListModel::refreshGifFrames);
    }
    if (hasGif) {
        catalog.start();
    } else {
        catalog.stop();
    }
}

void MessageListModel::refreshGifFrames()
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).kind == MediaKind::Gif) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::setUploadProgress(const QString &mediaId, int percent)
{
    if (mediaId.isEmpty()) {
        return;
    }
    m_uploadPercents.insert(mediaId, percent);
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).mediaId == mediaId) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::refreshMedia(const QString &mediaId)
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).mediaId == mediaId) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::setSearchMatches(const QSet<QString> &clientMsgIds,
                                        const QString &query)
{
    m_searchMatches = clientMsgIds;
    m_searchQuery = query;
    if (!m_messages.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0));
    }
}

void MessageListModel::clearSearchMatches()
{
    if (m_searchMatches.isEmpty() && m_searchQuery.isEmpty()) {
        return;
    }
    m_searchMatches.clear();
    m_searchQuery.clear();
    if (!m_messages.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0));
    }
}

bool MessageListModel::isSearchMatch(const QString &clientMsgId) const
{
    return m_searchMatches.contains(clientMsgId) || clientMsgId == m_highlighted;
}

void MessageListModel::setHighlighted(const QString &clientMsgId)
{
    if (m_highlighted == clientMsgId) {
        return;
    }
    const QString previous = m_highlighted;
    m_highlighted = clientMsgId;
    for (const QString &id : {previous, clientMsgId}) {
        const int row = indexOf(id);
        if (row >= 0) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::setReactions(const QString &chatId,
                                    const QVector<Reaction> &reactions)
{
    Q_UNUSED(chatId);
    QHash<QString, QVariantList> grouped;
    QHash<QString, QHash<QString, int>> counts;
    QHash<QString, QHash<QString, bool>> mine;
    const QString self = m_selfLogin;
    for (const Reaction &reaction : reactions) {
        counts[reaction.clientMsgId][reaction.emoji] += 1;
        if (reaction.senderLogin.compare(self, Qt::CaseInsensitive) == 0) {
            mine[reaction.clientMsgId][reaction.emoji] = true;
        }
        grouped[reaction.clientMsgId]; // ensure the key exists
    }
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        QVariantList chips;
        const QHash<QString, int> &byEmoji = it.value();
        for (auto emoji = byEmoji.cbegin(); emoji != byEmoji.cend(); ++emoji) {
            QVariantMap chip;
            chip.insert(QStringLiteral("emoji"), emoji.key());
            chip.insert(QStringLiteral("count"), emoji.value());
            chip.insert(QStringLiteral("mine"),
                        mine.value(it.key()).value(emoji.key(), false));
            chips.append(chip);
        }
        grouped.insert(it.key(), chips);
    }
    if (grouped == m_reactions) {
        return;
    }
    const QHash<QString, QVariantList> previous = m_reactions;
    m_reactions = grouped;
    // Repaint only rows whose chip band changed.
    for (int row = 0; row < m_messages.size(); ++row) {
        const QString id = m_messages.at(row).clientMsgId;
        if (previous.value(id) != m_reactions.value(id)) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::setPollResult(const PollResult &result)
{
    if (result.poll.pollId.isEmpty()) {
        return;
    }
    m_polls.insert(result.poll.pollId, result);
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).pollId == result.poll.pollId) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
}

void MessageListModel::clearPolls()
{
    if (m_polls.isEmpty()) {
        return;
    }
    m_polls.clear();
    if (!m_messages.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0));
    }
}

void MessageListModel::insertSorted(const Message &message)
{
    int row = m_messages.size();
    const qint64 ts = effectiveTs(message);
    for (int i = 0; i < m_messages.size(); ++i) {
        if (effectiveTs(m_messages.at(i)) > ts) {
            row = i;
            break;
        }
    }
    beginInsertRows({}, row, row);
    m_messages.insert(row, message);
    endInsertRows();
}

qint64 MessageListModel::effectiveTs(const Message &message)
{
    return message.serverTs > 0 ? message.serverTs : message.localTs;
}

MessageBubbleDelegate::MessageBubbleDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize MessageBubbleDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
{
    const BubbleLayout layout = measureBubble(option, index);
    return QSize(option.widget ? option.widget->width() : 640, layout.height);
}

void MessageBubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    const Tokens tokens = Theme::instance().tokens();
    const bool outgoing = index.data(MessageListModel::OutgoingRole).toBool();
    const QString body = index.data(MessageListModel::BodyRole).toString();
    const QString forwarded = index.data(MessageListModel::ForwardedRole).toString();
    const QString searchQuery = index.data(MessageListModel::SearchQueryRole).toString();
    const bool searchMatch = index.data(MessageListModel::SearchMatchRole).toBool();
    const bool edited = index.data(MessageListModel::EditedRole).toBool();
    const QString replyId = index.data(MessageListModel::ReplyRole).toString();
    QString reply = index.data(MessageListModel::ReplyTextRole).toString();
    if (reply.isEmpty()) {
        reply = replyId;
    }
    const qint64 ts = index.data(MessageListModel::TimeRole).toLongLong();
    const auto state = static_cast<MessageState>(index.data(MessageListModel::StateRole).toInt());
    const BubbleLayout layout = measureBubble(option, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (layout.isSystem) {
        // §38.4: centred grey line, no bubble and no actions.
        painter->setPen(tokens.textTertiary);
        painter->setFont(metaFont());
        painter->drawText(option.rect.adjusted(12, 0, -12, 0), Qt::AlignCenter, body);
        painter->restore();
        return;
    }

    const int left = outgoing ? option.rect.right() - layout.width - 12
                              : option.rect.left() + 12;
    const int top = option.rect.top() + 3;
    const QRect bubbleRect(left, top, layout.width,
                           layout.height - 6 - layout.reactionHeight);

    painter->setPen(Qt::NoPen);
    if (layout.kind == MediaKind::Sticker) {
        painter->setBrush(Qt::NoBrush);
    } else {
        painter->setBrush(outgoing ? tokens.bgBubbleOut : tokens.bgBubbleIn);
        painter->drawRoundedRect(bubbleRect, 14, 14);
    }
    if (searchMatch && layout.kind != MediaKind::Sticker) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(tokens.accent, 2));
        painter->drawRoundedRect(bubbleRect.adjusted(1, 1, -1, -1), 13, 13);
    }

    if (layout.isPoll) {
        const PollGeometry geometry = pollGeometry(option, index, layout);
        const QStringList options =
            index.data(MessageListModel::PollOptionsRole).toStringList();
        const QVariantList counts = index.data(MessageListModel::PollCountsRole).toList();
        const QVariantList mine = index.data(MessageListModel::PollMyChoicesRole).toList();
        const int flags = index.data(MessageListModel::PollFlagsRole).toInt();
        const int correct = index.data(MessageListModel::PollCorrectRole).toInt();
        const int total = index.data(MessageListModel::PollTotalRole).toInt();
        const bool multiple = (flags & 0x1) != 0;
        const bool anonymous = (flags & 0x2) != 0;
        const bool quiz = (flags & 0x4) != 0;
        const bool closed = (flags & 0x8) != 0;
        const bool canClose = (flags & 0x10) != 0;
        const bool reveal = quiz && (closed || !mine.isEmpty());

        painter->setPen(tokens.textPrimary);
        painter->setFont(messageFont());
        painter->drawText(geometry.question, Qt::TextWordWrap | Qt::AlignLeft
                                                | Qt::AlignTop, body);

        for (int i = 0; i < options.size() && i < geometry.options.size(); ++i) {
            const QRect row = geometry.options.at(i);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.bgInput);
            painter->drawRoundedRect(row, 8, 8);
            const int count = i < counts.size() ? counts.at(i).toInt() : 0;
            const int percent = total > 0 ? count * 100 / total : 0;
            if (percent > 0) {
                painter->setBrush(QColor(tokens.accent.red(), tokens.accent.green(),
                                         tokens.accent.blue(), 60));
                painter->drawRoundedRect(
                    QRect(row.left(), row.top(), qMax(6, row.width() * percent / 100),
                          row.height()),
                    8, 8);
            }
            const bool myChoice = mine.contains(i);
            if (myChoice) {
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(tokens.accent, 2));
                painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 7, 7);
            } else if (reveal && i == correct) {
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(tokens.online, 2));
                painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 7, 7);
            }
            painter->setPen(tokens.textPrimary);
            painter->setFont(messageFont());
            painter->drawText(row.adjusted(10, 0, -54, 0),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(options.at(i),
                                                               Qt::ElideRight,
                                                               row.width() - 64));
            painter->setPen(tokens.textSecondary);
            painter->setFont(metaFont());
            painter->drawText(row.adjusted(0, 0, -10, 0),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QStringLiteral("%1%").arg(percent));
        }

        // Footer: total votes plus the poll flags.
        QStringList footer;
        footer.append(tr("%n vote(s)", "", total));
        if (anonymous) {
            footer.append(tr("Anonymous"));
        }
        if (multiple) {
            footer.append(tr("Multiple answers"));
        }
        if (closed) {
            footer.append(tr("Closed"));
        }
        painter->setPen(tokens.textTertiary);
        painter->setFont(metaFont());
        painter->drawText(geometry.footer.adjusted(0, 0, canClose ? -56 : 0, 0),
                          Qt::AlignLeft | Qt::AlignVCenter, footer.join(QStringLiteral(" · ")));
        if (canClose) {
            painter->setPen(tokens.accent);
            painter->drawText(geometry.close, Qt::AlignRight | Qt::AlignVCenter,
                              tr("Close"));
        }

        const QRect metaRect(bubbleRect.left() + kPadX,
                             bubbleRect.bottom() - kPadY - kMetaHeight,
                             bubbleRect.width() - kPadX * 2, kMetaHeight);
        painter->setFont(metaFont());
        painter->setPen(tokens.textTertiary);
        painter->drawText(metaRect, Qt::AlignRight | Qt::AlignVCenter, formatTime(ts));

        const QVector<ReactionChipBox> chips =
            reactionChips(option, index, bubbleRect.left(), bubbleRect.bottom() + 6,
                          option.widget ? option.widget->width() - 24 : 616);
        paintReactionChips(painter, chips,
                           index.data(MessageListModel::ReactionsRole).toList(), tokens);
        painter->restore();
        return;
    }

    int textTop = bubbleRect.top() + kPadY;
    if (!reply.isEmpty()) {
        painter->setPen(tokens.textTertiary);
        QFont quoteFont = metaFont();
        quoteFont.setItalic(true);
        painter->setFont(quoteFont);
        const QFontMetrics quoteMetrics(quoteFont);
        painter->drawText(QRect(bubbleRect.left() + kPadX, textTop,
                                bubbleRect.width() - kPadX * 2, kReplyHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          quoteMetrics.elidedText(QStringLiteral("> %1").arg(reply),
                                                  Qt::ElideRight,
                                                  bubbleRect.width() - kPadX * 2));
        textTop += kReplyHeight;
    }

    if (!forwarded.isEmpty()) {
        painter->setPen(tokens.textTertiary);
        QFont forwardFont = metaFont();
        forwardFont.setItalic(true);
        painter->setFont(forwardFont);
        const QFontMetrics forwardMetrics(forwardFont);
        painter->drawText(
            QRect(bubbleRect.left() + kPadX, textTop, bubbleRect.width() - kPadX * 2,
                  layout.forwardHeight),
            Qt::AlignLeft | Qt::AlignVCenter,
            forwardMetrics.elidedText(tr("Forwarded from %1").arg(forwarded),
                                      Qt::ElideRight, bubbleRect.width() - kPadX * 2));
        textTop += layout.forwardHeight + 2;
    }

    if (layout.hasMedia) {
        const QRect mediaRect(bubbleRect.left() + kPadX, textTop,
                              bubbleRect.width() - kPadX * 2, layout.mediaHeight);
        const int percent = index.data(MessageListModel::UploadPercentRole).toInt();
        const QPixmap thumb = index.data(MessageListModel::ThumbRole).value<QPixmap>();

        if (layout.kind == MediaKind::Sticker) {
            const QPixmap sticker =
                index.data(MessageListModel::StickerPixmapRole).value<QPixmap>();
            if (!sticker.isNull()) {
                const QPixmap scaled = sticker.scaled(mediaRect.size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation);
                painter->drawPixmap(mediaRect.center().x() - scaled.width() / 2,
                                    mediaRect.center().y() - scaled.height() / 2, scaled);
            } else {
                painter->setPen(tokens.textTertiary);
                painter->setFont(metaFont());
                painter->drawText(mediaRect, Qt::AlignCenter,
                                  QCoreApplication::translate("MessageBubble", "Sticker"));
            }
        } else if (layout.kind == MediaKind::Gif) {
            QPainterPath clip;
            clip.addRoundedRect(mediaRect, 10, 10);
            painter->save();
            painter->setClipPath(clip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.bgInput);
            painter->drawRect(mediaRect);
            const QPixmap frame =
                index.data(MessageListModel::GifFrameRole).value<QPixmap>();
            if (!frame.isNull()) {
                drawPixmapCover(painter, mediaRect, frame);
            } else {
                const QPixmap icon = Icons::pixmap(QStringLiteral("image"),
                                                   tokens.textTertiary, 40);
                painter->drawPixmap(mediaRect.center().x() - icon.width() / 2,
                                    mediaRect.center().y() - icon.height() / 2, icon);
            }
            painter->restore();
            const QRect badge(mediaRect.left() + 8, mediaRect.top() + 8, 38, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 150));
            painter->drawRoundedRect(badge, 9, 9);
            painter->setPen(Qt::white);
            painter->setFont(metaFont());
            painter->drawText(badge, Qt::AlignCenter, QStringLiteral("GIF"));
        } else if (layout.kind == MediaKind::Album) {
            const QStringList ids =
                index.data(MessageListModel::AlbumIdsRole).toStringList();
            const QVariantList thumbs =
                index.data(MessageListModel::AlbumThumbsRole).toList();
            const QVector<QRect> cells = albumCells(ids.size(), mediaRect);
            for (int i = 0; i < cells.size(); ++i) {
                QPainterPath clip;
                clip.addRoundedRect(cells.at(i), 8, 8);
                painter->save();
                painter->setClipPath(clip);
                painter->setPen(Qt::NoPen);
                painter->setBrush(tokens.bgInput);
                painter->drawRect(cells.at(i));
                const QPixmap thumb = i < thumbs.size()
                    ? thumbs.at(i).value<QPixmap>()
                    : QPixmap();
                if (!thumb.isNull()) {
                    drawPixmapCover(painter, cells.at(i), thumb);
                } else {
                    const QPixmap icon = Icons::pixmap(QStringLiteral("image"),
                                                       tokens.textTertiary, 28);
                    painter->drawPixmap(cells.at(i).center().x() - icon.width() / 2,
                                        cells.at(i).center().y() - icon.height() / 2, icon);
                }
                painter->restore();
            }
        } else if (layout.kind == MediaKind::File) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.bgInput);
            painter->drawRoundedRect(mediaRect, 10, 10);
            const QPixmap icon = Icons::pixmap(QStringLiteral("archive"),
                                               tokens.textSecondary, 28);
            painter->drawPixmap(mediaRect.left() + 12,
                                mediaRect.center().y() - icon.height() / 2, icon);
            const QString name =
                index.data(MessageListModel::MediaNameRole).toString();
            const qint64 size = index.data(MessageListModel::MediaSizeRole).toLongLong();
            const QRect textRect(mediaRect.left() + 52, mediaRect.top() + 8,
                                 mediaRect.width() - 64, mediaRect.height() - 16);
            painter->setPen(tokens.textPrimary);
            painter->setFont(messageFont());
            painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), 20),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(
                                  name.isEmpty() ? QCoreApplication::translate("MessageBubble", "File") : name,
                                  Qt::ElideMiddle, textRect.width()));
            painter->setPen(tokens.textTertiary);
            painter->setFont(metaFont());
            painter->drawText(QRect(textRect.left(), textRect.top() + 22,
                                    textRect.width(), 16),
                              Qt::AlignLeft | Qt::AlignVCenter, formatSize(size));
            if (percent >= 0 && percent < 100) {
                painter->setPen(tokens.accent);
                painter->drawText(QRect(mediaRect.left(), mediaRect.top(),
                                        mediaRect.width() - 12, mediaRect.height()),
                                  Qt::AlignRight | Qt::AlignVCenter,
                                  QStringLiteral("%1%").arg(percent));
            }
        } else {
            QPainterPath clip;
            clip.addRoundedRect(mediaRect, 10, 10);
            painter->save();
            painter->setClipPath(clip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.bgInput);
            painter->drawRect(mediaRect);
            if (!thumb.isNull()) {
                drawPixmapCover(painter, mediaRect, thumb);
            } else {
                const QPixmap icon = Icons::pixmap(QStringLiteral("image"),
                                                   tokens.textTertiary, 40);
                painter->drawPixmap(mediaRect.center().x() - icon.width() / 2,
                                    mediaRect.center().y() - icon.height() / 2, icon);
            }
            painter->restore();

            if (layout.kind == MediaKind::Video) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0, 0, 0, 130));
                const QPoint center = mediaRect.center();
                painter->drawEllipse(center, 24, 24);
                QPainterPath play;
                play.moveTo(center.x() - 6, center.y() - 10);
                play.lineTo(center.x() + 11, center.y());
                play.lineTo(center.x() - 6, center.y() + 10);
                play.closeSubpath();
                painter->setBrush(Qt::white);
                painter->drawPath(play);
                const QString duration =
                    formatDuration(index.data(MessageListModel::DurationRole).toLongLong());
                if (!duration.isEmpty()) {
                    const QRect badge(mediaRect.right() - 52, mediaRect.bottom() - 24, 44,
                                      18);
                    painter->setBrush(QColor(0, 0, 0, 150));
                    painter->drawRoundedRect(badge, 9, 9);
                    painter->setPen(Qt::white);
                    painter->setFont(metaFont());
                    painter->drawText(badge, Qt::AlignCenter, duration);
                }
            }

            if (percent >= 0 && percent < 100) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0, 0, 0, 130));
                painter->drawRoundedRect(mediaRect, 10, 10);
                painter->setPen(Qt::white);
                QFont progressFont = messageFont();
                progressFont.setPixelSize(Theme::instance().scaledPx(18));
                painter->setFont(progressFont);
                painter->drawText(mediaRect, Qt::AlignCenter,
                                  QStringLiteral("%1%").arg(percent));
                const QRect bar(mediaRect.left() + 18, mediaRect.bottom() - 18,
                                mediaRect.width() - 36, 4);
                painter->setBrush(QColor(255, 255, 255, 70));
                painter->drawRoundedRect(bar, 2, 2);
                painter->setBrush(tokens.accent);
                painter->drawRoundedRect(
                    QRect(bar.left(), bar.top(), bar.width() * percent / 100, bar.height()),
                    2, 2);
            }
        }
        textTop += layout.mediaHeight;
        if (layout.captionHeight > 0) {
            textTop += 6;
        }
    }

    if (!body.isEmpty() && layout.kind != MediaKind::Sticker
        && layout.kind != MediaKind::Gif) {
        const QRect bodyRect(bubbleRect.left() + kPadX, textTop,
                             bubbleRect.width() - kPadX * 2,
                             layout.captionHeight + 2);
        if (!markdown::looksFormatted(body)) {
            painter->setPen(tokens.textPrimary);
            painter->setFont(messageFont());
            painter->drawText(bodyRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                              body);
        } else {
            markdown::paint(painter, bodyRect, body, messageFont(), tokens.textPrimary,
                            tokens.textSecondary, tokens.accent, tokens.bgInput,
                            searchMatch ? searchQuery : QString());
        }
    }

    const QRect metaRect(bubbleRect.left() + kPadX,
                         bubbleRect.bottom() - kPadY - kMetaHeight,
                         bubbleRect.width() - kPadX * 2, kMetaHeight);
    painter->setFont(metaFont());
    painter->setPen(tokens.textTertiary);
    const QString metaText = edited ? tr("%1 · edited").arg(formatTime(ts))
                                    : formatTime(ts);
    painter->drawText(metaRect, Qt::AlignRight | Qt::AlignVCenter, metaText);

    const QString iconName = statusIcon(state);
    if (!iconName.isEmpty()) {
        const QPixmap icon = Icons::pixmap(iconName, statusColor(state, tokens), 12);
        const int iconRight = metaRect.right()
            - painter->fontMetrics().horizontalAdvance(metaText) - 6;
        painter->drawPixmap(iconRight - icon.width(),
                            metaRect.center().y() - icon.height() / 2, icon);
    }

    // §38.1: chips sit under the bubble, aligned with its left edge.
    const QVector<ReactionChipBox> chips =
        reactionChips(option, index, bubbleRect.left(), bubbleRect.bottom() + 6,
                      option.widget ? option.widget->width() - 24 : 616);
    paintReactionChips(painter, chips, index.data(MessageListModel::ReactionsRole).toList(),
                       tokens);

    painter->restore();
}

bool reactionChipAt(const QStyleOptionViewItem &option, const QModelIndex &index,
                    const QPoint &pos, QString *emoji)
{
    const BubbleLayout layout = measureBubble(option, index);
    if (!layout.hasReactions || layout.isSystem) {
        return false;
    }
    const bool outgoing = index.data(MessageListModel::OutgoingRole).toBool();
    const int left = outgoing ? option.rect.right() - layout.width - 12
                              : option.rect.left() + 12;
    const int top = option.rect.top() + 3 + layout.height - 6 - layout.reactionHeight + 6;
    const QVector<ReactionChipBox> chips =
        reactionChips(option, index, left, top,
                      option.widget ? option.widget->width() - 24 : 616);
    for (const ReactionChipBox &chip : chips) {
        if (chip.rect.contains(pos)) {
            if (emoji) {
                *emoji = chip.emoji;
            }
            return true;
        }
    }
    return false;
}

bool pollOptionAt(const QStyleOptionViewItem &option, const QModelIndex &index,
                  const QPoint &pos, int *optionIndex)
{
    const BubbleLayout layout = measureBubble(option, index);
    if (!layout.isPoll) {
        return false;
    }
    const PollGeometry geometry = pollGeometry(option, index, layout);
    for (int i = 0; i < geometry.options.size(); ++i) {
        if (geometry.options.at(i).contains(pos)) {
            if (optionIndex) {
                *optionIndex = i;
            }
            return true;
        }
    }
    return false;
}

bool pollCloseAt(const QStyleOptionViewItem &option, const QModelIndex &index,
                 const QPoint &pos)
{
    const BubbleLayout layout = measureBubble(option, index);
    if (!layout.isPoll) {
        return false;
    }
    const int flags = index.data(MessageListModel::PollFlagsRole).toInt();
    if ((flags & 0x10) == 0) {
        return false;
    }
    return pollGeometry(option, index, layout).close.contains(pos);
}

} // namespace mini
