#pragma once

#include "domain/Chat.h"
#include "domain/Message.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QListView;
class QPoint;
class QStackedLayout;

namespace mini {

class Composer;
class EmptyState;
class MessageListModel;

// Right column: chat header, virtualised message history, reply preview and
// composer. Status ticks are painted by MessageBubbleDelegate.
class ChatView : public QWidget
{
    Q_OBJECT

public:
    explicit ChatView(QWidget *parent = nullptr);

    [[nodiscard]] Composer *composer() const { return m_composer; }
    [[nodiscard]] EmptyState *emptyState() const { return m_empty; }
    [[nodiscard]] QLabel *titleLabel() const { return m_title; }
    [[nodiscard]] QLabel *subtitleLabel() const { return m_subtitle; }
    [[nodiscard]] QListView *messageView() const { return m_messages; }
    [[nodiscard]] MessageListModel *messageModel() const { return m_model; }
    [[nodiscard]] QString chatId() const { return m_chatId; }

    void setChatTitle(const QString &title);
    void setChatSubtitle(const QString &subtitle);
    void setChat(const Chat &chat);
    // Header presence: "login · online" or "login · last seen 5m ago".
    void setPeerPresence(bool online, qint64 lastActiveAt);
    void clearChat();

    void setMessages(const QVector<Message> &messages);
    void appendMessage(const Message &message);
    void updateMessage(const Message &message);
    void removeMessage(const QString &clientMsgId);

    void setDraft(const QString &text);
    [[nodiscard]] QString draft() const;

    void setReplyTo(const QString &clientMsgId, const QString &text);
    void clearReply();
    [[nodiscard]] QString replyToClientMsgId() const;

signals:
    // The single send path: MainWindow reads replyToClientMsgId() itself.
    void sendRequested(const QString &text);
    void replyRequested(const QString &clientMsgId);
    void copyRequested(const QString &text);
    void deleteMessageRequested(const QString &clientMsgId);
    void cancelSendRequested(const QString &clientMsgId);
    void draftChanged(const QString &text);

private:
    void submitMessage(const QString &text);
    void showMessageMenu(const QPoint &pos);
    void resolveReplyPreviews();
    void refreshEmptyState();

    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    EmptyState *m_empty = nullptr;
    QListView *m_messages = nullptr;
    MessageListModel *m_model = nullptr;
    QStackedLayout *m_stack = nullptr;
    Composer *m_composer = nullptr;
    QString m_chatId;
    QString m_peerLogin;
    bool m_peerOnline = false;
    qint64 m_peerLastActiveAt = 0;
};

} // namespace mini
