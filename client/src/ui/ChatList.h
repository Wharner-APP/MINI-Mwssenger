#pragma once

#include "domain/Chat.h"
#include "services/SearchService.h"

#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QFrame;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QListView;
class QMenu;
class QStackedLayout;
class QToolButton;

namespace mini {

class ChatListModel;
class EmptyState;

namespace services {
class ChatService;
class SearchService;
}

// Left column list: previews, time, unread, pin/mute, filters, context menu,
// global Ctrl+K search and Ctrl+Tab switching. Virtualised by QListView.
class ChatList : public QWidget
{
    Q_OBJECT

public:
    enum class FilterMode { All, Unread, Pinned };

    explicit ChatList(QWidget *parent = nullptr);

    [[nodiscard]] ChatListModel *model() const { return m_model; }
    [[nodiscard]] QListView *view() const { return m_view; }
    [[nodiscard]] EmptyState *emptyState() const { return m_empty; }
    [[nodiscard]] QFrame *searchPanel() const { return m_searchPanel; }
    [[nodiscard]] QLineEdit *searchField() const { return m_searchField; }
    [[nodiscard]] QListWidget *searchResults() const { return m_searchResults; }
    [[nodiscard]] QToolButton *filterButton(FilterMode mode) const;

    void setStubCount(int count);
    void setFilter(const QString &text);
    [[nodiscard]] bool isEmpty() const;

    // Presence arrives asynchronously from WS snapshots/updates.
    void setPresence(const QString &login, bool online, qint64 lastActiveAt = 0);
    [[nodiscard]] QStringList peerLogins() const;
    [[nodiscard]] QPair<bool, qint64> presenceFor(const QString &login) const;

    void setChats(const QVector<Chat> &chats);
    [[nodiscard]] QVector<Chat> chats() const { return m_chats; }
    [[nodiscard]] Chat chatById(const QString &chatId) const;
    void updateChat(const Chat &chat);
    void removeChat(const QString &chatId);

    void setFilterMode(FilterMode mode);
    [[nodiscard]] FilterMode filterMode() const { return m_filterMode; }

    void setChatService(services::ChatService *service);
    [[nodiscard]] services::ChatService *chatService() const { return m_chatService; }
    void setSearchService(services::SearchService *service);
    [[nodiscard]] services::SearchService *searchService() const { return m_searchService; }

    // Menu with the eight chat actions; also shown on right click.
    [[nodiscard]] QMenu *buildChatMenu(const QString &chatId);

    void openSearch();
    void closeSearch();
    void setSearchResults(const services::SearchResults &results);
    void selectNext();
    void selectPrevious();

signals:
    void chatSelected(const QString &chatId);
    void messageSelected(const QString &chatId, const QString &clientMsgId);
    void personSelected(const QString &login);
    void newChatRequested();
    void globalSearchRequested(const QString &query);
    void pinToggled(const QString &chatId, bool pinned);
    void muteToggled(const QString &chatId, bool muted);
    void markReadRequested(const QString &chatId);
    void historyClearRequested(const QString &chatId);
    void chatDeleteRequested(const QString &chatId, bool deleteHistory);

private:
    void updateEmptyState();
    void rebuildItems();
    void reloadChats();
    void showContextMenu(const QPoint &pos);
    void confirmDeleteChat(const QString &chatId);
    void activateSearchItem(QListWidgetItem *item);
    void selectRelative(int delta);

    ChatListModel *m_model = nullptr;
    QListView *m_view = nullptr;
    EmptyState *m_empty = nullptr;
    QStackedLayout *m_stack = nullptr;
    QFrame *m_searchPanel = nullptr;
    QLineEdit *m_searchField = nullptr;
    QListWidget *m_searchResults = nullptr;
    QToolButton *m_allFilter = nullptr;
    QToolButton *m_unreadFilter = nullptr;
    QToolButton *m_pinnedFilter = nullptr;
    QButtonGroup *m_filterGroup = nullptr;
    services::ChatService *m_chatService = nullptr;
    services::SearchService *m_searchService = nullptr;
    QVector<Chat> m_chats;
    QHash<QString, QPair<bool, qint64>> m_presence;
    FilterMode m_filterMode = FilterMode::All;
};

} // namespace mini
