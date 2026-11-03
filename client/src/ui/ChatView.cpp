#include "ui/ChatView.h"

#include "services/MediaService.h"
#include "services/StickerService.h"
#include "domain/Disappearing.h"
#include "ui/Composer.h"
#include "ui/EmojiPanel.h"
#include "ui/Markdown.h"
#include "ui/MarkdownPainter.h"
#include "ui/MessageBubble.h"
#include "ui/Theme.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Icons.h"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QShortcut>
#include <QStackedLayout>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

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

    m_timerButton = new QToolButton(header);
    m_timerButton->setObjectName(QStringLiteral("chatDisappearingButton"));
    m_timerButton->setFixedSize(32, 32);
    m_timerButton->setIconSize(QSize(18, 18));
    m_timerButton->setCursor(Qt::PointingHandCursor);
    m_timerButton->setToolTip(tr("Disappearing messages"));
    m_timerButton->setAccessibleName(tr("Disappearing messages"));
    m_timerButton->setIcon(Icons::icon(QStringLiteral("clock"),
                                       Theme::instance().tokens().textSecondary, 18));
    m_timerButton->setVisible(false);
    headerLayout->addWidget(m_timerButton);
    connect(m_timerButton, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("disappearingMenu"));
        const QVector<qint64> values = disappearing::values();
        for (const qint64 value : values) {
            QAction *action = menu.addAction(disappearing::label(value));
            action->setCheckable(true);
            action->setChecked(value == m_disappearingMs);
            connect(action, &QAction::triggered, this, [this, value] {
                emit disappearingChanged(value);
            });
        }
        menu.exec(m_timerButton->mapToGlobal(QPoint(0, m_timerButton->height())));
    });

    m_info = new QToolButton(header);
    m_info->setObjectName(QStringLiteral("chatInfoButton"));
    m_info->setFixedSize(32, 32);
    m_info->setIconSize(QSize(18, 18));
    m_info->setCursor(Qt::PointingHandCursor);
    m_info->setToolTip(tr("Chat information"));
    m_info->setAccessibleName(tr("Chat information"));
    m_info->setIcon(Icons::icon(QStringLiteral("info"),
                                Theme::instance().tokens().textSecondary, 18));
    m_info->setVisible(false);
    headerLayout->addWidget(m_info);
    connect(m_info, &QToolButton::clicked, this, &ChatView::chatInfoRequested);

    m_pinBanner = new QFrame(this);
    m_pinBanner->setObjectName(QStringLiteral("pinnedBanner"));
    m_pinBanner->setFrameShape(QFrame::NoFrame);
    m_pinBanner->setFixedHeight(34);
    m_pinBanner->setVisible(false);
    m_pinLabel = new QLabel(m_pinBanner);
    m_pinLabel->setObjectName(QStringLiteral("pinnedBannerLabel"));
    m_pinLabel->setProperty("miniRole", QStringLiteral("caption"));
    m_pinLabel->setCursor(Qt::PointingHandCursor);
    auto *pinListButton = new QToolButton(m_pinBanner);
    pinListButton->setObjectName(QStringLiteral("pinnedListButton"));
    pinListButton->setText(tr("List"));
    pinListButton->setAutoRaise(true);
    auto *pinClose = new QToolButton(m_pinBanner);
    pinClose->setObjectName(QStringLiteral("pinnedBannerClose"));
    pinClose->setText(QStringLiteral("×"));
    pinClose->setAutoRaise(true);
    auto *pinLayout = new QHBoxLayout(m_pinBanner);
    pinLayout->setContentsMargins(16, 0, 12, 0);
    pinLayout->setSpacing(8);
    pinLayout->addWidget(m_pinLabel, 1);
    pinLayout->addWidget(pinListButton);
    pinLayout->addWidget(pinClose);
    connect(m_pinLabel, &QLabel::linkActivated, this, [this] { showPinnedList(); });
    connect(pinListButton, &QToolButton::clicked, this, &ChatView::showPinnedList);
    connect(pinClose, &QToolButton::clicked, this,
            [this] { m_pinBanner->setVisible(false); });

    m_searchBar = new QFrame(this);
    m_searchBar->setObjectName(QStringLiteral("chatSearchBar"));
    m_searchBar->setFrameShape(QFrame::NoFrame);
    m_searchBar->setVisible(false);
    m_searchField = new QLineEdit(m_searchBar);
    m_searchField->setObjectName(QStringLiteral("chatSearchField"));
    m_searchField->setPlaceholderText(tr("Search in chat"));
    m_searchField->setClearButtonEnabled(true);
    m_searchCounter = new QLabel(m_searchBar);
    m_searchCounter->setObjectName(QStringLiteral("chatSearchCounter"));
    m_searchCounter->setProperty("miniRole", QStringLiteral("caption"));
    m_searchPrev = new QToolButton(m_searchBar);
    m_searchPrev->setObjectName(QStringLiteral("chatSearchPrev"));
    m_searchPrev->setText(QStringLiteral("↑"));
    m_searchPrev->setAutoRaise(true);
    m_searchNext = new QToolButton(m_searchBar);
    m_searchNext->setObjectName(QStringLiteral("chatSearchNext"));
    m_searchNext->setText(QStringLiteral("↓"));
    m_searchNext->setAutoRaise(true);
    auto *searchClose = new QToolButton(m_searchBar);
    searchClose->setObjectName(QStringLiteral("chatSearchClose"));
    searchClose->setText(QStringLiteral("×"));
    searchClose->setAutoRaise(true);
    auto *searchLayout = new QHBoxLayout(m_searchBar);
    searchLayout->setContentsMargins(16, 4, 12, 4);
    searchLayout->setSpacing(6);
    searchLayout->addWidget(m_searchField, 1);
    searchLayout->addWidget(m_searchCounter);
    searchLayout->addWidget(m_searchPrev);
    searchLayout->addWidget(m_searchNext);
    searchLayout->addWidget(searchClose);
    connect(m_searchField, &QLineEdit::textChanged, this, &ChatView::runSearch);
    connect(m_searchPrev, &QToolButton::clicked, this, &ChatView::searchPrevious);
    connect(m_searchNext, &QToolButton::clicked, this, &ChatView::searchNext);
    connect(searchClose, &QToolButton::clicked, this, &ChatView::closeSearch);

    m_multiBar = new QFrame(this);
    m_multiBar->setObjectName(QStringLiteral("multiSelectBar"));
    m_multiBar->setFrameShape(QFrame::NoFrame);
    m_multiBar->setVisible(false);
    m_multiLabel = new QLabel(m_multiBar);
    m_multiLabel->setObjectName(QStringLiteral("multiSelectCount"));
    m_multiLabel->setProperty("miniRole", QStringLiteral("caption"));
    auto *multiForward = new QToolButton(m_multiBar);
    multiForward->setObjectName(QStringLiteral("multiForwardButton"));
    multiForward->setText(tr("Forward"));
    multiForward->setAutoRaise(true);
    auto *multiDelete = new QToolButton(m_multiBar);
    multiDelete->setObjectName(QStringLiteral("multiDeleteButton"));
    multiDelete->setText(tr("Delete"));
    multiDelete->setAutoRaise(true);
    auto *multiCopy = new QToolButton(m_multiBar);
    multiCopy->setObjectName(QStringLiteral("multiCopyButton"));
    multiCopy->setText(tr("Copy"));
    multiCopy->setAutoRaise(true);
    auto *multiCancel = new QToolButton(m_multiBar);
    multiCancel->setObjectName(QStringLiteral("multiCancelButton"));
    multiCancel->setText(tr("Cancel"));
    multiCancel->setAutoRaise(true);
    auto *multiLayout = new QHBoxLayout(m_multiBar);
    multiLayout->setContentsMargins(16, 4, 12, 4);
    multiLayout->setSpacing(10);
    multiLayout->addWidget(m_multiLabel, 1);
    multiLayout->addWidget(multiForward);
    multiLayout->addWidget(multiDelete);
    multiLayout->addWidget(multiCopy);
    multiLayout->addWidget(multiCancel);
    connect(multiCancel, &QToolButton::clicked, this, &ChatView::exitMultiSelect);
    connect(multiForward, &QToolButton::clicked, this, [this] {
        const QStringList ids = selectedClientMsgIds();
        if (!ids.isEmpty()) {
            emit forwardSelectedRequested(ids);
        }
    });
    connect(multiDelete, &QToolButton::clicked, this, [this] {
        const QStringList ids = selectedClientMsgIds();
        if (!ids.isEmpty()) {
            emit deleteSelectedRequested(ids);
            exitMultiSelect();
        }
    });
    connect(multiCopy, &QToolButton::clicked, this, [this] {
        const QStringList ids = selectedClientMsgIds();
        if (ids.isEmpty()) {
            return;
        }
        QStringList lines;
        for (const QString &id : ids) {
            const int row = m_model->indexOf(id);
            if (row >= 0) {
                lines.append(markdown::plainText(m_model->messageAt(row).body));
            }
        }
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(lines.join(QLatin1Char('\n')));
        }
        emit copySelectedRequested(ids);
    });

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
    layout->addWidget(m_pinBanner);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_multiBar);
    layout->addLayout(m_stack, 1);
    layout->addWidget(m_composer);

    connect(m_composer, &Composer::sendRequested, this, &ChatView::submitMessage);
    connect(m_composer, &Composer::draftChanged, this, &ChatView::draftChanged);
    connect(m_composer, &Composer::attachRequested, this, &ChatView::attachRequested);
    connect(m_composer, &Composer::filesDropped, this, &ChatView::filesDropped);
    connect(m_composer, &Composer::stickerRequested, this, &ChatView::stickerRequested);
    connect(m_composer, &Composer::gifRequested, this, &ChatView::gifRequested);
    connect(m_composer, &Composer::pollRequested, this, &ChatView::pollRequested);
    connect(m_composer, &Composer::stickerInstallRequested, this,
            &ChatView::stickerInstallRequested);
    connect(m_composer, &Composer::stickerRemoveRequested, this,
            &ChatView::stickerRemoveRequested);
    connect(m_composer, &Composer::recentEmojiChanged, this,
            &ChatView::recentEmojiChanged);
    connect(m_composer->replyCloseButton(), &QToolButton::clicked, this,
            [this] { m_editId.clear(); });
    connect(m_messages, &QListView::customContextMenuRequested, this,
            &ChatView::showMessageMenu);
    connect(m_messages, &QListView::clicked, this, [this](const QModelIndex &index) {
        const auto kind =
            static_cast<MediaKind>(index.data(MessageListModel::KindRole).toInt());
        if (kind != MediaKind::Photo && kind != MediaKind::Video
            && kind != MediaKind::File && kind != MediaKind::Album) {
            return;
        }
        emit mediaActivated(index.data(MessageListModel::ClientMsgIdRole).toString());
    });

    setAcceptDrops(true);
    m_messages->viewport()->installEventFilter(this);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_messages->doItemsLayout();
        m_messages->viewport()->update();
        update();
    });

    connect(m_messages->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { refreshSelection(); });
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &ChatView::openSearch);

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
    m_baseSubtitle = subtitle;
    if (m_disappearingMs > 0) {
        m_subtitle->setText(
            subtitle + QStringLiteral(" · ")
            + tr("Disappearing messages: %1").arg(disappearing::label(m_disappearingMs)));
    } else {
        m_subtitle->setText(subtitle);
    }
}

void ChatView::setDisappearingAfter(qint64 milliseconds)
{
    m_disappearingMs = milliseconds;
    m_timerButton->setToolTip(
        milliseconds > 0
            ? tr("Disappearing messages: %1").arg(disappearing::label(milliseconds))
            : tr("Disappearing messages: off"));
    setChatSubtitle(m_baseSubtitle);
}

void ChatView::setChat(const Chat &chat)
{
    m_chatId = chat.chatId;
    cancelEditing();
    closeSearch();
    exitMultiSelect();
    setPinnedMessages({});
    m_mentionAllAllowed = false;
    m_peerLogin = chat.peerLogin;
    m_peerOnline = false;
    m_peerLastActiveAt = 0;
    const bool isGroup = chat.kind == QLatin1String("group");
    const bool isChannel = chat.kind == QLatin1String("channel");
    // §29.14: a bot chat is labelled "bot" and says it is not end-to-end
    // encrypted. The flag survives restarts through the `_bot` login suffix.
    const bool isBot = chat.isBot
        || (!isGroup && !isChannel
            && chat.peerLogin.toLower().endsWith(QLatin1String("_bot")));
    m_title->setText(chat.title.isEmpty() ? chat.peerLogin : chat.title);
    setChatSubtitle(isChannel ? tr("Channel")
                              : (isGroup ? tr("Group")
                                         : (isBot
                                                ? tr("Bot · messages are not end-to-end encrypted")
                                                : (chat.peerLogin.isEmpty()
                                                       ? tr("Not connected")
                                                       : chat.peerLogin))));
    m_timerButton->setVisible(!chat.chatId.isEmpty());
    setDisappearingAfter(chat.expiresAfterMs);
    m_info->setVisible((isGroup || isChannel) && !chat.chatId.isEmpty());
    // A subscriber cannot post; MainWindow enables the composer once the
    // channel role and permissions are known.
    m_composer->setComposerEnabled(!chat.chatId.isEmpty() && !isChannel);
    if (chat.chatId.isEmpty()) {
        m_model->setMessages({});
        clearReply();
    }
    refreshEmptyState();
}

void ChatView::setComposerEnabled(bool enabled)
{
    m_composer->setComposerEnabled(enabled && !m_chatId.isEmpty());
}

bool ChatView::composerEnabled() const
{
    return m_composer->editor()->isEnabled();
}

void ChatView::clearChat()
{
    m_chatId.clear();
    cancelEditing();
    closeSearch();
    exitMultiSelect();
    setPinnedMessages({});
    m_title->setText(tr("No chat selected"));
    m_subtitle->setText(tr("Not connected"));
    m_info->setVisible(false);
    m_timerButton->setVisible(false);
    m_disappearingMs = 0;
    m_baseSubtitle = tr("Not connected");
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

void ChatView::setSelfLogin(const QString &login)
{
    m_model->setSelfLogin(login);
}

void ChatView::setReactions(const QString &chatId, const QVector<Reaction> &reactions)
{
    m_model->setReactions(chatId, reactions);
}

void ChatView::setPollResult(const PollResult &result)
{
    m_model->setPollResult(result);
}

void ChatView::clearPolls()
{
    m_model->clearPolls();
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

void ChatView::setMediaService(services::MediaService *media)
{
    m_media = media;
    m_model->setMediaService(media);
    if (!media) {
        return;
    }
    connect(media, &services::MediaService::thumbnailReady, m_model,
            [this](const QString &mediaId) { m_model->refreshMedia(mediaId); });
    connect(media, &services::MediaService::uploadProgress, m_model,
            [this](const QString &mediaId, qint64 sent, qint64 total) {
                const int percent =
                    total > 0 ? static_cast<int>((sent * 100) / total) : 0;
                m_model->setUploadProgress(mediaId, percent);
            });
    connect(media, &services::MediaService::uploadFinished, m_model,
            [this](const QString &mediaId, bool ok, const QString &) {
                m_model->setUploadProgress(mediaId, ok ? 100 : -1);
                if (ok) {
                    m_model->refreshMedia(mediaId);
                }
            });
    connect(media, &services::MediaService::downloadFinished, m_model,
            [this](const QString &mediaId, const QString &, bool, const QString &) {
                m_model->refreshMedia(mediaId);
            });
    m_model->refreshMedia(QString());
}

void ChatView::setStickerService(services::StickerService *stickers)
{
    m_stickers = stickers;
    m_model->setStickerService(stickers);
    m_composer->setStickerService(stickers);
}

QStringList ChatView::localFiles(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls()) {
        return paths;
    }
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) {
            paths.append(path);
        }
    }
    return paths;
}

void ChatView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void ChatView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

void ChatView::dropEvent(QDropEvent *event)
{
    const QStringList files = localFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        emit filesDropped(files);
        return;
    }
    QWidget::dropEvent(event);
}

bool ChatView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_messages->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto *drag = static_cast<QDragEnterEvent *>(event);
            if (!localFiles(drag->mimeData()).isEmpty()) {
                drag->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragMove: {
            auto *drag = static_cast<QDragMoveEvent *>(event);
            if (!localFiles(drag->mimeData()).isEmpty()) {
                drag->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto *drop = static_cast<QDropEvent *>(event);
            const QStringList files = localFiles(drop->mimeData());
            if (!files.isEmpty()) {
                drop->acceptProposedAction();
                emit filesDropped(files);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            // §38.1 / §38.3: chip and poll-option clicks are handled before
            // the media double-click so a vote never opens a viewer.
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) {
                break;
            }
            const QModelIndex index = m_messages->indexAt(mouse->pos());
            if (!index.isValid()) {
                break;
            }
            QStyleOptionViewItem option;
            option.initFrom(m_messages);
            option.rect = m_messages->visualRect(index);
            option.widget = m_messages;
            const QString clientMsgId =
                index.data(MessageListModel::ClientMsgIdRole).toString();
            QString emoji;
            if (reactionChipAt(option, index, mouse->pos(), &emoji)) {
                emit reactionToggled(clientMsgId, emoji);
                return true;
            }
            int optionIndex = -1;
            if (pollOptionAt(option, index, mouse->pos(), &optionIndex)) {
                emit pollVoteRequested(index.data(MessageListModel::PollIdRole).toString(),
                                       optionIndex);
                return true;
            }
            if (pollCloseAt(option, index, mouse->pos())) {
                emit pollCloseRequested(
                    index.data(MessageListModel::PollIdRole).toString());
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    if (!m_mentionAllAllowed && markdown::containsAllMention(text)) {
        emit mentionDenied(text);
        return;
    }
    if (!m_editId.isEmpty()) {
        const QString id = m_editId;
        m_editId.clear();
        m_composer->clearReplyPreview();
        emit editRequested(id, text);
        return;
    }
    emit sendRequested(text);
    m_composer->clearReplyPreview();
}

void ChatView::startEditing(const QString &clientMsgId, const QString &originalText)
{
    if (clientMsgId.isEmpty()) {
        return;
    }
    m_editId = clientMsgId;
    m_composer->setReplyPreview(
        clientMsgId, tr("Editing: %1").arg(markdown::plainText(originalText).left(60)));
    m_composer->setText(originalText);
    m_composer->editor()->setFocus();
    m_composer->editor()->selectAll();
}

void ChatView::cancelEditing()
{
    if (m_editId.isEmpty()) {
        return;
    }
    m_editId.clear();
    m_composer->clearReplyPreview();
    m_composer->setText(QString());
}

void ChatView::setPinnedMessages(const QVector<Message> &pinned)
{
    m_pinned = pinned.mid(0, 20);
    m_pinnedIds.clear();
    for (const Message &message : m_pinned) {
        m_pinnedIds.append(message.clientMsgId);
    }
    updatePinnedBanner();
}

void ChatView::updatePinnedBanner()
{
    if (!m_pinBanner) {
        return;
    }
    if (m_pinned.isEmpty()) {
        m_pinBanner->setVisible(false);
        return;
    }
    const Message &first = m_pinned.first();
    const QString preview = markdown::plainText(first.body).simplified().left(50);
    m_pinLabel->setText(
        QStringLiteral("<a href=\"pin\" style=\"color:inherit;text-decoration:none;\">%1</a>")
            .arg((tr("%n pinned", "", m_pinned.size())
                  + (preview.isEmpty() ? QString()
                                       : QStringLiteral(" · %1").arg(preview)))
                     .toHtmlEscaped()));
    m_pinLabel->setTextFormat(Qt::RichText);
    m_pinBanner->setVisible(true);
}

void ChatView::showPinnedList()
{
    if (m_pinned.isEmpty()) {
        return;
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("pinnedListMenu"));
    for (const Message &message : std::as_const(m_pinned)) {
        QString preview = markdown::plainText(message.body).simplified();
        if (preview.isEmpty()) {
            preview = message.mediaName.isEmpty() ? tr("Attachment") : message.mediaName;
        }
        QAction *action = menu.addAction(preview.left(70));
        action->setObjectName(QStringLiteral("pinnedItemAction"));
        const QString id = message.clientMsgId;
        connect(action, &QAction::triggered, this, [this, id] { jumpToMessage(id); });
    }
    menu.exec(m_pinBanner->mapToGlobal(QPoint(0, m_pinBanner->height())));
}

void ChatView::openSearch()
{
    m_searchBar->setVisible(true);
    m_searchField->setFocus();
    m_searchField->selectAll();
}

void ChatView::closeSearch()
{
    if (!m_searchBar) {
        return;
    }
    m_searchBar->setVisible(false);
    m_searchField->clear();
    m_matchIds.clear();
    m_currentMatch.clear();
    m_matchIndex = 0;
    m_model->clearSearchMatches();
    updateSearchCounter();
}

void ChatView::runSearch(const QString &query)
{
    const QString needle = query.trimmed();
    m_matchIds.clear();
    m_currentMatch.clear();
    m_matchIndex = 0;
    if (needle.isEmpty()) {
        m_model->clearSearchMatches();
        updateSearchCounter();
        return;
    }
    QSet<QString> matches;
    for (int row = 0; row < m_model->count(); ++row) {
        const Message message = m_model->messageAt(row);
        if (message.body.contains(needle, Qt::CaseInsensitive)) {
            m_matchIds.append(message.clientMsgId);
            matches.insert(message.clientMsgId);
        }
    }
    m_model->setSearchMatches(matches, needle);
    if (!m_matchIds.isEmpty()) {
        m_currentMatch = m_matchIds.first();
        jumpToMessage(m_currentMatch);
    }
    updateSearchCounter();
}

void ChatView::updateSearchCounter()
{
    if (!m_searchCounter) {
        return;
    }
    if (m_matchIds.isEmpty()) {
        m_searchCounter->setText(m_searchField && !m_searchField->text().trimmed().isEmpty()
                                     ? tr("0 of 0")
                                     : QString());
        return;
    }
    m_searchCounter->setText(
        tr("%1 of %2").arg(m_matchIndex + 1).arg(m_matchIds.size()));
}

void ChatView::searchNext()
{
    if (m_matchIds.isEmpty()) {
        return;
    }
    m_matchIndex = (m_matchIndex + 1) % m_matchIds.size();
    m_currentMatch = m_matchIds.at(m_matchIndex);
    jumpToMessage(m_currentMatch);
    updateSearchCounter();
}

void ChatView::searchPrevious()
{
    if (m_matchIds.isEmpty()) {
        return;
    }
    m_matchIndex = (m_matchIndex - 1 + m_matchIds.size()) % m_matchIds.size();
    m_currentMatch = m_matchIds.at(m_matchIndex);
    jumpToMessage(m_currentMatch);
    updateSearchCounter();
}

void ChatView::jumpToMessage(const QString &clientMsgId)
{
    const int row = m_model->indexOf(clientMsgId);
    if (row < 0) {
        return;
    }
    const QModelIndex index = m_model->index(row, 0);
    m_messages->scrollTo(index, QAbstractItemView::PositionAtCenter);
    m_messages->setCurrentIndex(index);
    m_model->setHighlighted(clientMsgId);
    m_currentMatch = clientMsgId;
}

void ChatView::enterMultiSelect(const QString &firstClientMsgId)
{
    m_multiSelect = true;
    m_messages->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_multiBar->setVisible(true);
    if (!firstClientMsgId.isEmpty()) {
        const int row = m_model->indexOf(firstClientMsgId);
        if (row >= 0) {
            m_messages->selectionModel()->select(
                m_model->index(row, 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    refreshSelection();
    m_messages->setFocus();
}

void ChatView::exitMultiSelect()
{
    if (!m_multiSelect && (!m_multiBar || !m_multiBar->isVisible())) {
        return;
    }
    m_multiSelect = false;
    if (m_messages->selectionModel()) {
        m_messages->selectionModel()->clearSelection();
    }
    m_messages->setSelectionMode(QAbstractItemView::SingleSelection);
    if (m_multiBar) {
        m_multiBar->setVisible(false);
    }
}

QStringList ChatView::selectedClientMsgIds() const
{
    QStringList ids;
    if (!m_messages->selectionModel()) {
        return ids;
    }
    QModelIndexList rows = m_messages->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &left, const QModelIndex &right) {
        return left.row() < right.row();
    });
    for (const QModelIndex &index : std::as_const(rows)) {
        const QString id = index.data(MessageListModel::ClientMsgIdRole).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}

void ChatView::refreshSelection()
{
    if (!m_multiLabel) {
        return;
    }
    m_multiLabel->setText(m_multiSelect
                              ? tr("%n selected", "", selectedClientMsgIds().size())
                              : QString());
}

bool ChatView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            key->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void ChatView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_multiSelect) {
            exitMultiSelect();
            return;
        }
        if (m_searchBar->isVisible()) {
            closeSearch();
            return;
        }
        if (!m_editId.isEmpty()) {
            cancelEditing();
            return;
        }
    }
    QWidget::keyPressEvent(event);
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
    const auto kind = static_cast<MediaKind>(index.data(MessageListModel::KindRole).toInt());
    const qint64 ts = index.data(MessageListModel::TimeRole).toLongLong();
    const bool withinWindow =
        ts > 0 && QDateTime::currentMSecsSinceEpoch() - ts <= 48LL * 60 * 60 * 1000;
    const bool canEdit =
        outgoing && kind == MediaKind::Text && withinWindow
        && state != MessageState::DeletedLocal;
    const bool canDeleteAll = m_mentionAllAllowed || (outgoing && withinWindow);
    const bool pinned = m_pinnedIds.contains(clientMsgId);

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("messageMenu"));

    QAction *reply = menu.addAction(tr("Reply"));
    reply->setObjectName(QStringLiteral("messageReplyAction"));
    QAction *edit = menu.addAction(tr("Edit"));
    edit->setObjectName(QStringLiteral("messageEditAction"));
    edit->setEnabled(canEdit);
    QAction *forward = menu.addAction(tr("Forward"));
    forward->setObjectName(QStringLiteral("messageForwardAction"));
    QAction *copy = menu.addAction(tr("Copy"));
    copy->setObjectName(QStringLiteral("messageCopyAction"));
    // §38.1: the twelve defaults live inside the menu, "More…" opens the
    // full emoji panel.
    QMenu *react = menu.addMenu(tr("React"));
    react->setObjectName(QStringLiteral("messageReactMenu"));
    for (const QString &emoji : defaultReactions()) {
        QAction *action = react->addAction(emoji);
        connect(action, &QAction::triggered, this, [this, clientMsgId, emoji] {
            emit reactionToggled(clientMsgId, emoji);
        });
    }
    QAction *moreReactions = react->addAction(tr("More…"));
    moreReactions->setObjectName(QStringLiteral("messageReactMoreAction"));
    connect(moreReactions, &QAction::triggered, this, [this, clientMsgId] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Reactions"));
        auto *panel = new EmojiPanel(&dialog);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(panel);
        connect(panel, &EmojiPanel::emojiSelected, this,
                [this, &dialog, clientMsgId](const QString &emoji) {
                    emit reactionToggled(clientMsgId, emoji);
                    dialog.accept();
                });
        dialog.resize(420, 360);
        dialog.exec();
    });
    QAction *pin = menu.addAction(pinned ? tr("Unpin") : tr("Pin"));
    pin->setObjectName(QStringLiteral("messagePinAction"));
    QAction *select = menu.addAction(tr("Select"));
    select->setObjectName(QStringLiteral("messageSelectAction"));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Delete for me"));
    remove->setObjectName(QStringLiteral("messageDeleteAction"));
    QAction *removeAll = menu.addAction(tr("Delete for everyone"));
    removeAll->setObjectName(QStringLiteral("messageDeleteAllAction"));
    removeAll->setEnabled(canDeleteAll);
    QAction *cancel = menu.addAction(tr("Cancel sending"));
    cancel->setObjectName(QStringLiteral("messageCancelAction"));
    cancel->setEnabled(outgoing && state == MessageState::Queued);

    connect(reply, &QAction::triggered, this, [this, clientMsgId, body] {
        setReplyTo(clientMsgId, body);
        emit replyRequested(clientMsgId);
    });
    connect(edit, &QAction::triggered, this,
            [this, clientMsgId, body] { startEditing(clientMsgId, body); });
    connect(forward, &QAction::triggered, this,
            [this, clientMsgId] { emit forwardRequested(clientMsgId); });
    connect(copy, &QAction::triggered, this, [this, body] {
        const QString plain = markdown::plainText(body);
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(plain);
        }
        emit copyRequested(body);
    });
    connect(pin, &QAction::triggered, this, [this, clientMsgId, pinned] {
        if (pinned) {
            emit unpinRequested(clientMsgId);
        } else {
            emit pinRequested(clientMsgId);
        }
    });
    connect(select, &QAction::triggered, this,
            [this, clientMsgId] { enterMultiSelect(clientMsgId); });
    connect(remove, &QAction::triggered, this, [this, clientMsgId] {
        removeMessage(clientMsgId);
        emit deleteMessageRequested(clientMsgId);
    });
    connect(removeAll, &QAction::triggered, this,
            [this, clientMsgId] { emit deleteForEveryoneRequested(clientMsgId); });
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
        const Message quotedMessage = m_model->messageAt(quotedRow);
        const QString quoted = mediaPreviewText(quotedMessage.kind,
                                                quotedMessage.mediaName,
                                                quotedMessage.body);
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
