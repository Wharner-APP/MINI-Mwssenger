#pragma once

#include "domain/Chat.h"

#include <QMainWindow>
#include <QSet>

class QAction;
class QMenu;
class QSplitter;
class QSystemTrayIcon;

namespace mini {

class ChatView;
class ProfilePanel;
class Rail;
class Settings;
class SettingsDialog;
class Sidebar;

namespace services {
class ChatService;
class ContactService;
class DiagnosticsService;
class MessageService;
class SearchService;
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
    void setMessageService(services::MessageService *service);
    void setSearchService(services::SearchService *service);
    void setStorageWorker(storage::StorageWorker *worker);
    void setWsClient(net::WsClient *ws);

    // Opens the direct chat for an exact login (R438i). Public so the shell
    // and the tests can drive it without a modal input dialog.
    void openDirectChatForLogin(const QString &login);
    void openProfile();
    void openDiagnostics();

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
    void ensureDiagnosticsService();
    void openSettings();
    void promptNewChat();
    void openChat(const Chat &chat);
    void openChatById(const QString &chatId);
    void refreshChatList();
    void saveDraft();
    void sendMessage(const QString &text);
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
    services::MessageService *m_messageService = nullptr;
    services::SearchService *m_searchService = nullptr;
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
    bool m_quitting = false;
    int m_trayAvailableOverride = -1;
};

} // namespace mini
