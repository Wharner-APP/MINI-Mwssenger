#include "ui/ChatList.h"

#include "services/ChatService.h"
#include "ui/ChatItemDelegate.h"
#include "ui/ChatListModel.h"
#include "ui/Theme.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Icons.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace mini {

using services::ChatService;
using services::SearchService;

namespace {

QString initialsFor(const QString &title)
{
    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return QStringLiteral("?");
    }
    QString initials = words.first().left(1).toUpper();
    if (words.size() > 1) {
        initials += words.at(1).left(1).toUpper();
    }
    return initials;
}

QString timeFor(qint64 milliseconds)
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

ChatItem itemFor(const Chat &chat)
{
    ChatItem item;
    item.id = chat.chatId;
    item.title = chat.title.isEmpty() ? chat.peerLogin : chat.title;
    if (item.title.isEmpty()) {
        item.title = chat.chatId;
    }
    item.preview = chat.lastMessagePreview;
    item.time = timeFor(chat.lastMessageTs);
    item.initials = initialsFor(item.title);
    item.unread = chat.unreadCount;
    item.pinned = chat.pinned;
    item.muted = chat.muted;
    return item;
}

QToolButton *makeFilterButton(const QString &objectName, const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

constexpr int kSearchKindRole = Qt::UserRole;
constexpr int kSearchChatRole = Qt::UserRole + 1;
constexpr int kSearchMessageRole = Qt::UserRole + 2;
constexpr int kSearchLoginRole = Qt::UserRole + 3;

} // namespace

ChatList::ChatList(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("chatList"));

    m_model = new ChatListModel(this);
    m_view = new QListView(this);
    m_view->setObjectName(QStringLiteral("chatListView"));
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ChatItemDelegate(this));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformItemSizes(true);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setFocusPolicy(Qt::StrongFocus);
    m_view->setMouseTracking(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setAccessibleName(tr("Chats"));

    m_empty = new EmptyState(this);
    m_empty->setIconName(QStringLiteral("chats"));
    m_empty->setTitle(tr("No chats yet"));
    m_empty->setDescription(tr("Start a conversation: create an account or find someone by login."));
    m_empty->setActionText(tr("New chat"));

    m_stack = new QStackedLayout();
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_view);
    m_stack->addWidget(m_empty);

    auto *stackHost = new QWidget(this);
    stackHost->setLayout(m_stack);

    m_allFilter = makeFilterButton(QStringLiteral("filterAll"), tr("All"), this);
    m_unreadFilter = makeFilterButton(QStringLiteral("filterUnread"), tr("Unread"), this);
    m_pinnedFilter = makeFilterButton(QStringLiteral("filterPinned"), tr("Pinned"), this);
    m_allFilter->setChecked(true);
    m_filterGroup = new QButtonGroup(this);
    m_filterGroup->setExclusive(true);
    m_filterGroup->addButton(m_allFilter);
    m_filterGroup->addButton(m_unreadFilter);
    m_filterGroup->addButton(m_pinnedFilter);

    auto *filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(6);
    filterRow->addWidget(m_allFilter);
    filterRow->addWidget(m_unreadFilter);
    filterRow->addWidget(m_pinnedFilter);
    filterRow->addStretch(1);

    m_searchPanel = new QFrame(this);
    m_searchPanel->setObjectName(QStringLiteral("globalSearchPanel"));
    m_searchPanel->setFrameShape(QFrame::NoFrame);
    m_searchField = new QLineEdit(m_searchPanel);
    m_searchField->setObjectName(QStringLiteral("globalSearchField"));
    m_searchField->setPlaceholderText(tr("Search chats and messages"));
    m_searchField->setAccessibleName(tr("Global search"));
    m_searchField->setClearButtonEnabled(true);
    m_searchResults = new QListWidget(m_searchPanel);
    m_searchResults->setObjectName(QStringLiteral("globalSearchResults"));
    m_searchResults->setMaximumHeight(320);
    auto *searchLayout = new QVBoxLayout(m_searchPanel);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(4);
    searchLayout->addWidget(m_searchField);
    searchLayout->addWidget(m_searchResults);
    m_searchPanel->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addLayout(filterRow);
    layout->addWidget(m_searchPanel);
    layout->addWidget(stackHost, 1);

    connect(m_view, &QListView::clicked, this, [this](const QModelIndex &index) {
        const QString id = index.data(ChatListModel::IdRole).toString();
        if (!id.isEmpty()) {
            emit chatSelected(id);
        }
    });
    connect(m_view, &QListView::activated, this, [this](const QModelIndex &index) {
        const QString id = index.data(ChatListModel::IdRole).toString();
        if (!id.isEmpty()) {
            emit chatSelected(id);
        }
    });
    connect(m_view, &QListView::customContextMenuRequested, this, &ChatList::showContextMenu);
    connect(m_empty, &EmptyState::actionTriggered, this, &ChatList::newChatRequested);

    connect(m_filterGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *button) {
        if (button == m_unreadFilter) {
            setFilterMode(FilterMode::Unread);
        } else if (button == m_pinnedFilter) {
            setFilterMode(FilterMode::Pinned);
        } else {
            setFilterMode(FilterMode::All);
        }
    });

    connect(m_searchField, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString query = text.trimmed();
        emit globalSearchRequested(query);
        if (!m_searchService) {
            m_searchResults->clear();
            return;
        }
        if (query.isEmpty()) {
            m_searchResults->clear();
            return;
        }
        m_searchService->search(query, [this, query](Result<services::SearchResults> result) {
            if (m_searchField->text().trimmed() != query) {
                return;
            }
            setSearchResults(result.isOk() ? result.value() : services::SearchResults{});
        });
    });
    connect(m_searchResults, &QListWidget::itemClicked, this, &ChatList::activateSearchItem);
    connect(m_searchResults, &QListWidget::itemActivated, this, &ChatList::activateSearchItem);

    auto *searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    searchShortcut->setContext(Qt::ApplicationShortcut);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        if (m_searchPanel->isVisible()) {
            closeSearch();
        } else {
            openSearch();
        }
    });

    auto *nextShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    nextShortcut->setContext(Qt::ApplicationShortcut);
    connect(nextShortcut, &QShortcut::activated, this, &ChatList::selectNext);

    auto *previousShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    previousShortcut->setContext(Qt::ApplicationShortcut);
    connect(previousShortcut, &QShortcut::activated, this, &ChatList::selectPrevious);

    auto *altDown = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), this);
    altDown->setContext(Qt::ApplicationShortcut);
    connect(altDown, &QShortcut::activated, this, &ChatList::selectNext);

    auto *altUp = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), this);
    altUp->setContext(Qt::ApplicationShortcut);
    connect(altUp, &QShortcut::activated, this, &ChatList::selectPrevious);

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &ChatList::closeSearch);

    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_view->doItemsLayout();
        m_view->viewport()->update();
    });

    updateEmptyState();
}

QToolButton *ChatList::filterButton(FilterMode mode) const
{
    switch (mode) {
    case FilterMode::Unread:
        return m_unreadFilter;
    case FilterMode::Pinned:
        return m_pinnedFilter;
    case FilterMode::All:
    default:
        return m_allFilter;
    }
}

void ChatList::setStubCount(int count)
{
    m_chats.clear();
    m_model->setStubCount(count);
    updateEmptyState();
}

void ChatList::setFilter(const QString &text)
{
    m_model->setFilter(text);
    updateEmptyState();
}

bool ChatList::isEmpty() const
{
    return m_model->rowCount() == 0;
}

void ChatList::setChats(const QVector<Chat> &chats)
{
    m_chats = chats;
    rebuildItems();
}

Chat ChatList::chatById(const QString &chatId) const
{
    for (const Chat &chat : m_chats) {
        if (chat.chatId == chatId) {
            return chat;
        }
    }
    return {};
}

void ChatList::updateChat(const Chat &chat)
{
    bool replaced = false;
    for (Chat &existing : m_chats) {
        if (existing.chatId == chat.chatId) {
            existing = chat;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_chats.append(chat);
    }
    rebuildItems();
}

void ChatList::removeChat(const QString &chatId)
{
    for (int i = 0; i < m_chats.size(); ++i) {
        if (m_chats.at(i).chatId == chatId) {
            m_chats.removeAt(i);
            break;
        }
    }
    rebuildItems();
}

void ChatList::setFilterMode(FilterMode mode)
{
    m_filterMode = mode;
    filterButton(mode)->setChecked(true);
    rebuildItems();
}

void ChatList::setChatService(ChatService *service)
{
    if (m_chatService == service) {
        return;
    }
    if (m_chatService) {
        disconnect(m_chatService, nullptr, this, nullptr);
    }
    m_chatService = service;
    if (m_chatService) {
        connect(m_chatService, &ChatService::chatsChanged, this, &ChatList::reloadChats);
        connect(m_chatService, &ChatService::chatRemoved, this, &ChatList::removeChat);
        reloadChats();
    }
}

void ChatList::setSearchService(SearchService *service)
{
    m_searchService = service;
}

void ChatList::reloadChats()
{
    if (!m_chatService) {
        return;
    }
    m_chatService->listChats([this](Result<QVector<Chat>> result) {
        if (result.isOk()) {
            setChats(result.value());
        }
    });
}

QMenu *ChatList::buildChatMenu(const QString &chatId)
{
    const Chat chat = chatById(chatId);
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("chatMenu"));

    QAction *open = menu->addAction(tr("Open"));
    open->setObjectName(QStringLiteral("chatOpenAction"));
    QAction *markRead = menu->addAction(tr("Mark as read"));
    markRead->setObjectName(QStringLiteral("chatMarkReadAction"));
    QAction *pin = menu->addAction(chat.pinned ? tr("Unpin") : tr("Pin"));
    pin->setObjectName(QStringLiteral("chatPinAction"));
    QAction *mute = menu->addAction(chat.muted ? tr("Unmute") : tr("Mute"));
    mute->setObjectName(QStringLiteral("chatMuteAction"));
    QAction *copyLogin = menu->addAction(tr("Copy login"));
    copyLogin->setObjectName(QStringLiteral("chatCopyLoginAction"));
    QAction *copyTitle = menu->addAction(tr("Copy name"));
    copyTitle->setObjectName(QStringLiteral("chatCopyTitleAction"));
    QAction *clear = menu->addAction(tr("Clear history"));
    clear->setObjectName(QStringLiteral("chatClearHistoryAction"));
    QAction *remove = menu->addAction(tr("Delete chat"));
    remove->setObjectName(QStringLiteral("chatDeleteAction"));

    connect(open, &QAction::triggered, this, [this, chatId] { emit chatSelected(chatId); });
    // The menu only reports the intent; MainWindow owns the service call, so
    // one click can never execute an action twice.
    connect(markRead, &QAction::triggered, this,
            [this, chatId] { emit markReadRequested(chatId); });
    connect(pin, &QAction::triggered, this, [this, chatId, pinned = chat.pinned] {
        emit pinToggled(chatId, !pinned);
    });
    connect(mute, &QAction::triggered, this, [this, chatId, muted = chat.muted] {
        emit muteToggled(chatId, !muted);
    });
    connect(copyLogin, &QAction::triggered, this, [chat] {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(chat.peerLogin);
        }
    });
    connect(copyTitle, &QAction::triggered, this, [chat] {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(chat.title);
        }
    });
    connect(clear, &QAction::triggered, this,
            [this, chatId] { emit historyClearRequested(chatId); });
    connect(remove, &QAction::triggered, this, [this, chatId] { confirmDeleteChat(chatId); });

    return menu;
}

void ChatList::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString chatId = index.data(ChatListModel::IdRole).toString();
    if (chatId.isEmpty()) {
        return;
    }
    QMenu *menu = buildChatMenu(chatId);
    menu->exec(m_view->viewport()->mapToGlobal(pos));
    menu->deleteLater();
}

void ChatList::confirmDeleteChat(const QString &chatId)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("deleteChatDialog"));
    dialog.setWindowTitle(tr("Delete chat"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *question = new QLabel(tr("Delete this chat? The messages can be kept."), &dialog);
    question->setWordWrap(true);
    auto *check = new QCheckBox(tr("Delete message history"), &dialog);
    check->setObjectName(QStringLiteral("deleteHistoryCheck"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Delete"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("deleteChatConfirm"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));

    layout->addWidget(question);
    layout->addWidget(check);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool deleteHistory = check->isChecked();
    emit chatDeleteRequested(chatId, deleteHistory);
}

void ChatList::openSearch()
{
    m_searchPanel->setVisible(true);
    m_searchField->setFocus();
    m_searchField->selectAll();
}

void ChatList::closeSearch()
{
    m_searchPanel->setVisible(false);
    m_searchResults->clear();
    m_searchField->clear();
}

void ChatList::setSearchResults(const services::SearchResults &results)
{
    m_searchResults->clear();
    for (const Chat &chat : results.chats) {
        auto *item = new QListWidgetItem(chat.title.isEmpty() ? chat.peerLogin : chat.title,
                                         m_searchResults);
        item->setData(kSearchKindRole, 0);
        item->setData(kSearchChatRole, chat.chatId);
    }
    for (const Message &message : results.messages) {
        const Chat chat = chatById(message.chatId);
        const QString title = chat.title.isEmpty() ? message.chatId : chat.title;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(title, message.body.left(60)), m_searchResults);
        item->setData(kSearchKindRole, 1);
        item->setData(kSearchChatRole, message.chatId);
        item->setData(kSearchMessageRole, message.clientMsgId);
    }
    for (const Contact &contact : results.people) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(contact.displayName, contact.login), m_searchResults);
        item->setData(kSearchKindRole, 2);
        item->setData(kSearchLoginRole, contact.login);
    }
}

void ChatList::activateSearchItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const int kind = item->data(kSearchKindRole).toInt();
    if (kind == 0) {
        emit chatSelected(item->data(kSearchChatRole).toString());
    } else if (kind == 1) {
        const QString chatId = item->data(kSearchChatRole).toString();
        emit chatSelected(chatId);
        emit messageSelected(chatId, item->data(kSearchMessageRole).toString());
    } else {
        emit personSelected(item->data(kSearchLoginRole).toString());
    }
    closeSearch();
}

void ChatList::selectNext()
{
    selectRelative(1);
}

void ChatList::selectPrevious()
{
    selectRelative(-1);
}

void ChatList::selectRelative(int delta)
{
    const int rows = m_model->rowCount();
    if (rows == 0) {
        return;
    }
    const QModelIndex current = m_view->currentIndex();
    int row = current.isValid() ? current.row() + delta : (delta > 0 ? 0 : rows - 1);
    row = (row % rows + rows) % rows;
    const QModelIndex index = m_model->index(row, 0);
    m_view->setCurrentIndex(index);
    const QString id = index.data(ChatListModel::IdRole).toString();
    if (!id.isEmpty()) {
        emit chatSelected(id);
    }
}

void ChatList::updateEmptyState()
{
    const bool listEmpty = m_model->rowCount() == 0;
    if (m_model->totalCount() == 0) {
        m_empty->setTitle(tr("No chats yet"));
        m_empty->setDescription(
            tr("Start a conversation: create an account or find someone by login."));
        m_empty->setActionText(tr("New chat"));
    } else if (listEmpty) {
        m_empty->setTitle(tr("No chats found"));
        m_empty->setDescription(tr("Try a different search."));
        m_empty->setActionText(QString());
    }
    m_stack->setCurrentWidget(listEmpty ? static_cast<QWidget *>(m_empty)
                                        : static_cast<QWidget *>(m_view));
}

void ChatList::setPresence(const QString &login, bool online, qint64 lastActiveAt)
{
    const QString key = login.trimmed().toLower();
    if (key.isEmpty()) {
        return;
    }
    m_presence.insert(key, qMakePair(online, lastActiveAt));
    rebuildItems();
}

QStringList ChatList::peerLogins() const
{
    QStringList logins;
    for (const Chat &chat : m_chats) {
        if (!chat.peerLogin.trimmed().isEmpty()
            && !logins.contains(chat.peerLogin, Qt::CaseInsensitive)) {
            logins.append(chat.peerLogin);
        }
    }
    return logins;
}

QPair<bool, qint64> ChatList::presenceFor(const QString &login) const
{
    return m_presence.value(login.trimmed().toLower(), qMakePair(false, qint64(0)));
}

void ChatList::rebuildItems()
{
    QVector<ChatItem> items;
    items.reserve(m_chats.size());
    for (const Chat &chat : m_chats) {
        if (m_filterMode == FilterMode::Unread && chat.unreadCount <= 0) {
            continue;
        }
        if (m_filterMode == FilterMode::Pinned && !chat.pinned) {
            continue;
        }
        ChatItem item = itemFor(chat);
        item.online = presenceFor(chat.peerLogin).first;
        items.append(item);
    }
    m_model->setItems(items);
    updateEmptyState();
}

} // namespace mini
