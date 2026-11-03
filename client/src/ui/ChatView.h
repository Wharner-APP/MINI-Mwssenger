#pragma once

#include "domain/Chat.h"
#include "domain/Message.h"

#include <QVector>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFrame;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListView;
class QMimeData;
class QModelIndex;
class QPoint;
class QStackedLayout;
class QToolButton;

namespace mini {

namespace services {
class MediaService;
class StickerService;
}

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
    [[nodiscard]] QToolButton *infoButton() const { return m_info; }
    [[nodiscard]] QString chatId() const { return m_chatId; }

    void setChatTitle(const QString &title);
    void setChatSubtitle(const QString &subtitle);
    void setChat(const Chat &chat);
    // Channels gate the composer by publish permission (§43.2); direct and
    // group chats keep it enabled whenever a chat is selected.
    void setComposerEnabled(bool enabled);
    [[nodiscard]] bool composerEnabled() const;
    // Header presence: "login · online" or "login · last seen 5m ago".
    void setPeerPresence(bool online, qint64 lastActiveAt);
    void clearChat();

    void setMessages(const QVector<Message> &messages);
    void appendMessage(const Message &message);
    void updateMessage(const Message &message);
    void removeMessage(const QString &clientMsgId);

    void setDraft(const QString &text);
    [[nodiscard]] QString draft() const;

    // Thumbnails, upload progress and the media viewer seam.
    void setMediaService(services::MediaService *media);
    [[nodiscard]] services::MediaService *mediaService() const { return m_media; }
    void setStickerService(services::StickerService *stickers);
    [[nodiscard]] services::StickerService *stickerService() const { return m_stickers; }

    void setReplyTo(const QString &clientMsgId, const QString &text);
    void clearReply();
    [[nodiscard]] QString replyToClientMsgId() const;

    // §35.4: @all is accepted only when the current group/channel role is
    // administrator. Direct chats keep this false.
    void setMentionAllAllowed(bool allowed) { m_mentionAllAllowed = allowed; }
    [[nodiscard]] bool mentionAllAllowed() const { return m_mentionAllAllowed; }

    // §37.3: composer edit mode. Editing replaces sending until cancelled.
    void startEditing(const QString &clientMsgId, const QString &originalText);
    void cancelEditing();
    [[nodiscard]] bool isEditing() const { return !m_editId.isEmpty(); }
    [[nodiscard]] QString editingClientMsgId() const { return m_editId; }

    // §38.2: pinned banner and list.
    void setPinnedMessages(const QVector<Message> &pinned);
    [[nodiscard]] QFrame *pinnedBanner() const { return m_pinBanner; }
    [[nodiscard]] int pinnedCount() const { return m_pinned.size(); }
    [[nodiscard]] const QVector<Message> &pinnedMessages() const { return m_pinned; }

    // §47.2: in-chat search over the loaded history.
    void openSearch();
    void closeSearch();
    [[nodiscard]] QFrame *searchBar() const { return m_searchBar; }
    [[nodiscard]] QLineEdit *searchField() const { return m_searchField; }
    [[nodiscard]] int searchMatchCount() const { return m_matchIds.size(); }
    [[nodiscard]] QString searchCurrentId() const { return m_currentMatch; }
    void searchNext();
    void searchPrevious();
    // §37.1/47.2: scrolls to a message and paints the accent highlight.
    void jumpToMessage(const QString &clientMsgId);

    // §37.6: multi-select toolbar.
    void enterMultiSelect(const QString &firstClientMsgId = QString());
    void exitMultiSelect();
    [[nodiscard]] bool multiSelectActive() const { return m_multiSelect; }
    [[nodiscard]] QFrame *multiToolbar() const { return m_multiBar; }
    [[nodiscard]] QStringList selectedClientMsgIds() const;

signals:
    // The single send path: MainWindow reads replyToClientMsgId() itself.
    void sendRequested(const QString &text);
    void replyRequested(const QString &clientMsgId);
    void copyRequested(const QString &text);
    void chatInfoRequested();
    void deleteMessageRequested(const QString &clientMsgId);
    void deleteForEveryoneRequested(const QString &clientMsgId);
    void editRequested(const QString &clientMsgId, const QString &text);
    void forwardRequested(const QString &clientMsgId);
    void pinRequested(const QString &clientMsgId);
    void unpinRequested(const QString &clientMsgId);
    void forwardSelectedRequested(const QStringList &clientMsgIds);
    void deleteSelectedRequested(const QStringList &clientMsgIds);
    void copySelectedRequested(const QStringList &clientMsgIds);
    void mentionDenied(const QString &text);
    void cancelSendRequested(const QString &clientMsgId);
    void draftChanged(const QString &text);
    void attachRequested();
    void filesDropped(const QStringList &paths);
    void stickerRequested(const QString &packId, const QString &stickerId);
    void gifRequested(const QString &gifId);
    void stickerInstallRequested();
    void stickerRemoveRequested(const QString &packId);
    void recentEmojiChanged(const QStringList &characters);
    // Double click / single click on a photo, video, file or album bubble.
    void mediaActivated(const QString &clientMsgId);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void submitMessage(const QString &text);
    [[nodiscard]] static QStringList localFiles(const QMimeData *mime);
    void showMessageMenu(const QPoint &pos);
    void resolveReplyPreviews();
    void refreshEmptyState();
    void updatePinnedBanner();
    void showPinnedList();
    void runSearch(const QString &query);
    void updateSearchCounter();
    void refreshSelection();
    void refreshSelectionMode();

    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QToolButton *m_info = nullptr;
    EmptyState *m_empty = nullptr;
    QListView *m_messages = nullptr;
    MessageListModel *m_model = nullptr;
    QStackedLayout *m_stack = nullptr;
    Composer *m_composer = nullptr;
    services::MediaService *m_media = nullptr;
    services::StickerService *m_stickers = nullptr;
    QString m_chatId;
    QString m_peerLogin;
    bool m_peerOnline = false;
    qint64 m_peerLastActiveAt = 0;
    bool m_mentionAllAllowed = false;
    QString m_editId;
    QVector<Message> m_pinned;
    QStringList m_pinnedIds;
    QFrame *m_pinBanner = nullptr;
    QLabel *m_pinLabel = nullptr;
    QFrame *m_searchBar = nullptr;
    QLineEdit *m_searchField = nullptr;
    QLabel *m_searchCounter = nullptr;
    QToolButton *m_searchPrev = nullptr;
    QToolButton *m_searchNext = nullptr;
    QStringList m_matchIds;
    QString m_currentMatch;
    int m_matchIndex = 0;
    QFrame *m_multiBar = nullptr;
    QLabel *m_multiLabel = nullptr;
    bool m_multiSelect = false;
};

} // namespace mini
