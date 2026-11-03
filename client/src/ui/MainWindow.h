#pragma once

#include "domain/Chat.h"
#include "domain/ChatFolder.h"
#include "domain/Message.h"

#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <QVector>

class QAction;
class QMenu;
class QSplitter;
class QSystemTrayIcon;

namespace mini {

class ChatView;
class GroupInfoPanel;
class MediaViewer;
class ProfilePanel;
class Rail;
class Settings;
class SettingsDialog;
class Sidebar;
class Toast;

namespace services {
class BotService;
struct BotInfo;
class ChannelService;
class ChatService;
class ContactService;
class DiagnosticsService;
class ExportService;
class FolderService;
class GroupService;
class ImportService;
class MediaService;
class MessageService;
class NotificationService;
struct NotificationEvent;
class PollService;
class PrivacyService;
class SearchService;
class StickerService;
struct MediaPreparation;
}

namespace storage {
class StorageWorker;
}

namespace net {
class WsClient;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Settings *settings = nullptr, QWidget *parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] Rail *rail() const { return m_rail; }
    [[nodiscard]] Sidebar *sidebar() const { return m_sidebar; }
    [[nodiscard]] ChatView *chatView() const { return m_chatView; }

    // Services are owned by the shell (Application/tests); the window only
    // connects them to widgets. Every setter is safe to call with nullptr.
    void setChatService(services::ChatService *service);
    void setContactService(services::ContactService *service);
    void setGroupService(services::GroupService *service);
    void setChannelService(services::ChannelService *service);
    void setMessageService(services::MessageService *service);
    // §38.3 / §48
    void setPollService(services::PollService *service);
    void setFolderService(services::FolderService *service);
    void setMediaService(services::MediaService *service);
    void setStickerService(services::StickerService *service);
    void setSearchService(services::SearchService *service);
    // §44 / §11.12: bots and privacy are optional until signed in.
    void setBotService(services::BotService *service);
    void setPrivacyService(services::PrivacyService *service);
    [[nodiscard]] services::BotService *botService() const { return m_botService; }
    [[nodiscard]] services::PrivacyService *privacyService() const { return m_privacyService; }
    // §46: notification rules, sounds, tray balloons and in-app toasts.
    void setNotificationService(services::NotificationService *service);
    [[nodiscard]] services::NotificationService *notificationService() const
    {
        return m_notifications;
    }
    [[nodiscard]] Toast *notificationToast() const { return m_toast; }
    // §49: encrypted export/import are wired into the settings dialog.
    void setExportService(services::ExportService *service);
    void setImportService(services::ImportService *service);
    [[nodiscard]] services::MediaService *mediaService() const { return m_media; }
    [[nodiscard]] services::StickerService *stickerService() const { return m_stickers; }
    [[nodiscard]] MediaViewer *mediaViewer() const { return m_viewer; }
    void setStorageWorker(storage::StorageWorker *worker);
    void setWsClient(net::WsClient *ws);

    // Opens the direct chat for an exact login (R438i). Public so the shell
    // and the tests can drive it without a modal input dialog.
    void openDirectChatForLogin(const QString &login);
    void openProfile();
    void openDiagnostics();
    // Group flows (S17 / S19). Both are also reachable from the sidebar menu
    // and the chat header button.
    void openNewGroupDialog();
    void openGroupInfo(const QString &groupId);
    void promptJoinGroup();
    // Channel flows (S18 / S20, §43).
    void openNewChannelDialog();
    void openChannelInfo(const QString &channelId);
    void promptJoinChannel();
    // §33.12 / §44: the owner's bot list and its chat.
    void openBotsDialog();
    void openBotChat(const services::BotInfo &bot);

    [[nodiscard]] QSystemTrayIcon *trayIcon() const { return m_tray; }
    [[nodiscard]] QMenu *trayMenu() const { return m_trayMenu; }
    [[nodiscard]] QAction *trayOpenAction() const { return m_trayOpen; }
    [[nodiscard]] QAction *trayDoNotDisturbAction() const { return m_trayDoNotDisturb; }
    [[nodiscard]] QAction *trayExitAction() const { return m_trayExit; }
    [[nodiscard]] QAction *traySignOutAction() const { return m_traySignOut; }

    [[nodiscard]] bool closeToTrayEnabled() const;
    void setCloseToTray(bool enabled);
    [[nodiscard]] bool doNotDisturb() const;
    void setTrayAvailableForTesting(bool available);

    void saveGeometryToSettings();

signals:
    void signOutRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupTray();
    void setupNotifications();
    void connectNotificationSignals();
    void handleIncomingNotification(const Message &message);
    void showToast(const QString &chatId, const QString &title, const QString &body);
    [[nodiscard]] bool messageMentionsSelf(const Message &message) const;
    void ensureDiagnosticsService();
    void openSettings();
    void promptNewChat();
    void openChat(const Chat &chat);
    void openChatById(const QString &chatId);
    void jumpToMessage(const QString &chatId, const QString &clientMsgId);
    void openForwardDialog(const QStringList &sourceIds);
    void refreshPins();
    void refreshChatList();
    // §38.1 / §38.3 / §39 / §48
    void refreshReactions(const QString &chatId);
    void refreshPollsForChat(const QString &chatId, const QVector<Message> &messages);
    void refreshPoll(const QString &pollId);
    void openPollDialog();
    void refreshFolders();
    void openFolderDialog(const ChatFolder &folder);
    void applyActiveFolder();
    void saveDraft();
    void sendMessage(const QString &text);
    // Media pipeline: pick/drop -> prepare -> preview -> upload -> sendMedia.
    void attachFiles();
    void handleFiles(const QStringList &paths);
    void handleAlbum(const QStringList &paths);
    void uploadAlbum(const QString &chatId, const QVector<services::MediaPreparation> &items,
                     const QString &caption, const QString &replyTo);
    void processNextFile();
    void sendSticker(const QString &packId, const QString &stickerId);
    void sendGif(const QString &gifId);
    void installStickerPack();
    void removeStickerPack(const QString &packId);
    void saveEmojiRecents(const QStringList &characters);
    void sendPreparedFile(const services::MediaPreparation &prep, const QString &path,
                          const QString &caption, bool asFile, const QString &replyTo);
    void openMedia(const QString &clientMsgId);
    void showViewer(const QString &mediaId, const QString &path);
    void showAlbumViewer(const QString &clientMsgId);
    [[nodiscard]] MediaViewer *ensureViewer();
    void saveMediaAs(const QString &mediaId);
    void setupShortcuts();
    void subscribePresence();
    void restoreFromTray();
    void quitFromTray();
    [[nodiscard]] bool trayAvailable() const;
    void connectMessageService();

    Settings *m_settings = nullptr;
    Rail *m_rail = nullptr;
    Sidebar *m_sidebar = nullptr;
    ChatView *m_chatView = nullptr;
    QSplitter *m_splitter = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    ProfilePanel *m_profilePanel = nullptr;
    services::DiagnosticsService *m_diagnostics = nullptr;

    services::ChatService *m_chatService = nullptr;
    services::ContactService *m_contactService = nullptr;
    services::GroupService *m_groupService = nullptr;
    services::ChannelService *m_channelService = nullptr;
    services::MessageService *m_messageService = nullptr;
    services::PollService *m_pollService = nullptr;
    services::FolderService *m_folderService = nullptr;
    services::MediaService *m_media = nullptr;
    services::StickerService *m_stickers = nullptr;
    services::SearchService *m_searchService = nullptr;
    services::BotService *m_botService = nullptr;
    services::PrivacyService *m_privacyService = nullptr;
    services::NotificationService *m_notifications = nullptr;
    services::ExportService *m_export = nullptr;
    services::ImportService *m_import = nullptr;
    MediaViewer *m_viewer = nullptr;
    Toast *m_toast = nullptr;
    QString m_toastChatId;
    QStringList m_fileQueue;
    Message m_pendingUpload;
    storage::StorageWorker *m_storage = nullptr;
    net::WsClient *m_ws = nullptr;
    QSet<QString> m_presenceLogins;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayOpen = nullptr;
    QAction *m_trayDoNotDisturb = nullptr;
    QAction *m_trayExit = nullptr;
    QAction *m_traySignOut = nullptr;

    Chat m_currentChat;
    // §44: the id of the bot whose chat is open, so messages take the bot
    // endpoint instead of the E2E outbox.
    QString m_currentBotId;
    QVector<ChatFolder> m_folders;
    ChatFolder m_activeFolder;
    QString m_pendingJumpId;
    bool m_quitting = false;
    int m_trayAvailableOverride = -1;
};

} // namespace mini
