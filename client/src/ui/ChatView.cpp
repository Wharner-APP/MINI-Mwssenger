#include "ui/ChatView.h"

#include "ui/Composer.h"
#include "ui/MessageBubble.h"
#include "ui/Theme.h"
#include "ui/widgets/EmptyState.h"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace mini {

ChatView::ChatView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("chatView"));
    setMinimumWidth(560);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setFixedHeight(64);

    m_title = new QLabel(tr("No chat selected"), header);
    m_title->setProperty("miniRole", QStringLiteral("subtitle"));
    m_subtitle = new QLabel(tr("Not connected"), header);
    m_subtitle->setProperty("miniRole", QStringLiteral("caption"));

    auto *headerText = new QVBoxLayout();
    headerText->setContentsMargins(0, 0, 0, 0);
    headerText->setSpacing(2);
    headerText->addWidget(m_title);
    headerText->addWidget(m_subtitle);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 10, 20, 10);
    headerLayout->addLayout(headerText, 1);

    m_model = new MessageListModel(this);
    m_messages = new QListView(this);
    m_messages->setObjectName(QStringLiteral("messageListView"));
    m_messages->setModel(m_model);
    m_messages->setItemDelegate(new MessageBubbleDelegate(this));
    m_messages->setFrameShape(QFrame::NoFrame);
    m_messages->setUniformItemSizes(false);
    m_messages->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messages->setSelectionMode(QAbstractItemView::SingleSelection);
    m_messages->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messages->setContextMenuPolicy(Qt::CustomContextMenu);
    m_messages->setAccessibleName(tr("Messages"));

    m_empty = new EmptyState(this);
    m_empty->setIconName(QStringLiteral("chats"));
    m_empty->setTitle(tr("Select a chat"));
    m_empty->setDescription(tr("Choose a conversation on the left, or start a new one."));

    m_stack = new QStackedLayout();
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_messages);

    m_composer = new Composer(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addLayout(m_stack, 1);
    layout->addWidget(m_composer);

    connect(m_composer, &Composer::sendRequested, this, &ChatView::submitMessage);
    connect(m_composer, &Composer::draftChanged, this, &ChatView::draftChanged);
    connect(m_messages, &QListView::customContextMenuRequested, this,
            &ChatView::showMessageMenu);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_messages->doItemsLayout();
        m_messages->viewport()->update();
        update();
    });

    refreshEmptyState();
}

void ChatView::setChatTitle(const QString &title)
{
    m_title->setText(title.isEmpty() ? tr("No chat selected") : title);
    m_composer->setComposerEnabled(!title.isEmpty());
    if (title.isEmpty()) {
        m_chatId.clear();
        m_model->setMessages({});
        clearReply();
    }
    refreshEmptyState();
}

void ChatView::setChatSubtitle(const QString &subtitle)
{
    m_subtitle->setText(subtitle);
}

void ChatView::setChat(const Chat &chat)
{
    m_chatId = chat.chatId;
    m_peerLogin = chat.peerLogin;
    m_peerOnline = false;
    m_peerLastActiveAt = 0;
    m_title->setText(chat.title.isEmpty() ? chat.peerLogin : chat.title);
    m_subtitle->setText(chat.peerLogin.isEmpty() ? tr("Not connected") : chat.peerLogin);
    m_composer->setComposerEnabled(!chat.chatId.isEmpty());
    if (chat.chatId.isEmpty()) {
        m_model->setMessages({});
        clearReply();
    }
    refreshEmptyState();
}

void ChatView::clearChat()
{
    m_chatId.clear();
    m_title->setText(tr("No chat selected"));
    m_subtitle->setText(tr("Not connected"));
    m_model->setMessages({});
    m_composer->setText(QString());
    m_composer->setComposerEnabled(false);
    clearReply();
    refreshEmptyState();
}

void ChatView::setPeerPresence(bool online, qint64 lastActiveAt)
{
    m_peerOnline = online;
    m_peerLastActiveAt = lastActiveAt;
    if (m_peerLogin.isEmpty()) {
        return;
    }
    QString state;
    if (online) {
        state = tr("online");
    } else if (lastActiveAt > 0) {
        const qint64 delta = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - lastActiveAt);
        const qint64 minutes = delta / 60000;
        if (minutes < 1) {
            state = tr("last seen just now");
        } else if (minutes < 60) {
            state = tr("last seen %1m ago").arg(minutes);
        } else if (minutes < 24 * 60) {
            state = tr("last seen %1h ago").arg(minutes / 60);
        } else {
            state = tr("last seen %1d ago").arg(minutes / (24 * 60));
        }
    } else {
        state = tr("offline");
    }
    m_subtitle->setText(QStringLiteral("%1 · %2").arg(m_peerLogin, state));
}

void ChatView::setMessages(const QVector<Message> &messages)
{
    m_model->setMessages(messages);
    resolveReplyPreviews();
    if (m_model->count() > 0) {
        m_messages->scrollToBottom();
    }
    refreshEmptyState();
}

void ChatView::appendMessage(const Message &message)
{
    m_model->appendMessage(message);
    resolveReplyPreviews();
    m_messages->scrollToBottom();
    refreshEmptyState();
}

void ChatView::updateMessage(const Message &message)
{
    m_model->updateMessage(message);
    resolveReplyPreviews();
}

void ChatView::removeMessage(const QString &clientMsgId)
{
    m_model->removeMessage(clientMsgId);
    refreshEmptyState();
}

void ChatView::setDraft(const QString &text)
{
    m_composer->setText(text);
}

QString ChatView::draft() const
{
    return m_composer->text();
}

void ChatView::setReplyTo(const QString &clientMsgId, const QString &text)
{
    m_composer->setReplyPreview(clientMsgId, text);
}

void ChatView::clearReply()
{
    m_composer->clearReplyPreview();
}

QString ChatView::replyToClientMsgId() const
{
    return m_composer->replyToClientMsgId();
}

void ChatView::submitMessage(const QString &text)
{
    emit sendRequested(text);
    m_composer->clearReplyPreview();
}

void ChatView::showMessageMenu(const QPoint &pos)
{
    const QModelIndex index = m_messages->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString clientMsgId = index.data(MessageListModel::ClientMsgIdRole).toString();
    const QString body = index.data(MessageListModel::BodyRole).toString();
    const bool outgoing = index.data(MessageListModel::OutgoingRole).toBool();
    const auto state = static_cast<MessageState>(index.data(MessageListModel::StateRole).toInt());

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("messageMenu"));

    QAction *reply = menu.addAction(tr("Reply"));
    reply->setObjectName(QStringLiteral("messageReplyAction"));
    QAction *copy = menu.addAction(tr("Copy"));
    copy->setObjectName(QStringLiteral("messageCopyAction"));
    QAction *remove = menu.addAction(tr("Delete for me"));
    remove->setObjectName(QStringLiteral("messageDeleteAction"));
    QAction *cancel = menu.addAction(tr("Cancel sending"));
    cancel->setObjectName(QStringLiteral("messageCancelAction"));
    cancel->setEnabled(outgoing && state == MessageState::Queued);

    connect(reply, &QAction::triggered, this, [this, clientMsgId, body] {
        setReplyTo(clientMsgId, body);
        emit replyRequested(clientMsgId);
    });
    connect(copy, &QAction::triggered, this, [this, body] {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(body);
        }
        emit copyRequested(body);
    });
    connect(remove, &QAction::triggered, this, [this, clientMsgId] {
        removeMessage(clientMsgId);
        emit deleteMessageRequested(clientMsgId);
    });
    connect(cancel, &QAction::triggered, this,
            [this, clientMsgId] { emit cancelSendRequested(clientMsgId); });

    menu.exec(m_messages->viewport()->mapToGlobal(pos));
}

void ChatView::resolveReplyPreviews()
{
    for (int row = 0; row < m_model->count(); ++row) {
        const Message message = m_model->messageAt(row);
        if (message.replyToClientMsgId.isEmpty()) {
            continue;
        }
        const int quotedRow = m_model->indexOf(message.replyToClientMsgId);
        if (quotedRow < 0) {
            continue;
        }
        const QString quoted = m_model->messageAt(quotedRow).body;
        m_model->setReplyPreview(message.clientMsgId, quoted.left(80));
    }
}

void ChatView::refreshEmptyState()
{
    const bool hasChat = !m_chatId.isEmpty()
        || (!m_title->text().isEmpty() && m_title->text() != tr("No chat selected"));
    const bool hasMessages = m_model->count() > 0;
    const bool showMessages = hasChat && hasMessages;
    m_stack->setCurrentWidget(showMessages ? static_cast<QWidget *>(m_messages)
                                           : static_cast<QWidget *>(m_empty));
    m_empty->setVisible(!showMessages);
}

} // namespace mini
