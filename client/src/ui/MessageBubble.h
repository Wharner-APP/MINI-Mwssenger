#pragma once

#include "domain/Message.h"

#include <QAbstractListModel>
#include <QHash>
#include <QStyledItemDelegate>
#include <QVector>

namespace mini {

// Virtualised history for ChatView: one row per message, painted by
// MessageBubbleDelegate. No widget is created per message.
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ClientMsgIdRole = Qt::UserRole + 1,
        ChatIdRole,
        BodyRole,
        TimeRole,
        OutgoingRole,
        StateRole,
        ReplyRole,
        ReplyTextRole,
        SenderRole,
    };

    explicit MessageListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    void setMessages(const QVector<Message> &messages);
    void appendMessage(const Message &message);
    void updateMessage(const Message &message);
    void removeMessage(const QString &clientMsgId);

    [[nodiscard]] Message messageAt(int row) const;
    [[nodiscard]] int indexOf(const QString &clientMsgId) const;
    [[nodiscard]] int count() const { return m_messages.size(); }

    // Short quote shown above a message that answers another one. The view
    // fills it from the history it already holds.
    void setReplyPreview(const QString &clientMsgId, const QString &text);

private:
    void insertSorted(const Message &message);
    [[nodiscard]] static qint64 effectiveTs(const Message &message);

    QVector<Message> m_messages;
    QHash<QString, QString> m_replyPreviews;
};

class MessageBubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageBubbleDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
};

} // namespace mini
