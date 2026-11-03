#include "ui/MessageBubble.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>

#include <algorithm>

namespace mini {

namespace {

constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kMetaHeight = 16;
constexpr int kReplyHeight = 20;

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
}

void MessageListModel::appendMessage(const Message &message)
{
    if (indexOf(message.clientMsgId) >= 0) {
        updateMessage(message);
        return;
    }
    insertSorted(message);
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
    const QString body = index.data(MessageListModel::BodyRole).toString();
    const QString replyId = index.data(MessageListModel::ReplyRole).toString();
    QString reply = index.data(MessageListModel::ReplyTextRole).toString();
    if (reply.isEmpty()) {
        reply = replyId;
    }

    const QFontMetrics metrics(messageFont());
    const int maxWidth = bubbleMaxWidth(option) - kPadX * 2;
    const int naturalWidth = metrics.horizontalAdvance(body);
    const int textWidth = qBound(40, naturalWidth, maxWidth);
    const int textHeight =
        metrics.boundingRect(QRect(0, 0, textWidth, 100000), Qt::TextWordWrap, body).height();

    int height = textHeight + kPadY * 2 + kMetaHeight;
    if (!reply.isEmpty()) {
        height += kReplyHeight;
    }
    return QSize(option.widget ? option.widget->width() : 640, qMax(38, height));
}

void MessageBubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    const Tokens tokens = Theme::instance().tokens();
    const bool outgoing = index.data(MessageListModel::OutgoingRole).toBool();
    const QString body = index.data(MessageListModel::BodyRole).toString();
    const QString replyId = index.data(MessageListModel::ReplyRole).toString();
    QString reply = index.data(MessageListModel::ReplyTextRole).toString();
    if (reply.isEmpty()) {
        reply = replyId;
    }
    const qint64 ts = index.data(MessageListModel::TimeRole).toLongLong();
    const auto state = static_cast<MessageState>(index.data(MessageListModel::StateRole).toInt());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QFontMetrics metrics(messageFont());
    const int maxBubble = bubbleMaxWidth(option);
    const int naturalWidth = metrics.horizontalAdvance(body) + kPadX * 2;
    const int bubbleWidth = qBound(160, naturalWidth, maxBubble);

    int height = metrics.boundingRect(QRect(0, 0, bubbleWidth - kPadX * 2, 100000),
                                      Qt::TextWordWrap, body)
                     .height()
        + kPadY * 2 + kMetaHeight;
    if (!reply.isEmpty()) {
        height += kReplyHeight;
    }
    height = qMax(height, 38);

    const int left = outgoing ? option.rect.right() - bubbleWidth - 12 : option.rect.left() + 12;
    const int top = option.rect.top() + 3;
    const QRect bubbleRect(left, top, bubbleWidth, height - 6);

    painter->setPen(Qt::NoPen);
    painter->setBrush(outgoing ? tokens.bgBubbleOut : tokens.bgBubbleIn);
    painter->drawRoundedRect(bubbleRect, 14, 14);

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

    painter->setPen(tokens.textPrimary);
    painter->setFont(messageFont());
    painter->drawText(QRect(bubbleRect.left() + kPadX, textTop,
                            bubbleRect.width() - kPadX * 2,
                            bubbleRect.height() - kPadY * 2 - kMetaHeight),
                      Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, body);

    const QRect metaRect(bubbleRect.left() + kPadX,
                         bubbleRect.bottom() - kPadY - kMetaHeight,
                         bubbleRect.width() - kPadX * 2, kMetaHeight);
    painter->setFont(metaFont());
    painter->setPen(tokens.textTertiary);
    painter->drawText(metaRect, Qt::AlignRight | Qt::AlignVCenter, formatTime(ts));

    const QString iconName = statusIcon(state);
    if (!iconName.isEmpty()) {
        const QPixmap icon = Icons::pixmap(iconName, statusColor(state, tokens), 12);
        const int iconRight = metaRect.right() - painter->fontMetrics().horizontalAdvance(
                                                     formatTime(ts))
            - 6;
        painter->drawPixmap(iconRight - icon.width(),
                            metaRect.center().y() - icon.height() / 2, icon);
    }

    painter->restore();
}

} // namespace mini
