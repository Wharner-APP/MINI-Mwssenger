#include "ui/MainWindow.h"

#include "app/Config.h"
#include "app/Paths.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "services/ChatService.h"
#include "services/ContactService.h"
#include "services/DiagnosticsService.h"
#include "net/WsClient.h"
#include "services/MessageService.h"
#include "services/SearchService.h"
#include "storage/StorageWorker.h"
#include "ui/ChatList.h"
#include "ui/ChatListModel.h"
#include "ui/ChatView.h"
#include "ui/Composer.h"
#include "ui/DiagnosticsDialog.h"
#include "ui/ProfilePanel.h"
#include "ui/Rail.h"
#include "ui/SettingsDialog.h"
#include "ui/Sidebar.h"

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

namespace mini {

namespace {
constexpr auto kGeometryKey = "window/geometry";
constexpr auto kCloseToTrayKey = "ui/close_to_tray";
constexpr auto kDoNotDisturbKey = "ui/do_not_disturb";
} // namespace

MainWindow::MainWindow(Settings *settings, QWidget *parent)
    : QMainWindow(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("MINI Messenger"));
    setMinimumSize(1000, 640);
    resize(1200, 760);

    auto *shell = new QWidget(this);
    shell->setObjectName(QStringLiteral("appShell"));

    m_rail = new Rail(shell);
    m_sidebar = new Sidebar(shell);
    m_chatView = new ChatView(shell);

    m_splitter = new QSplitter(Qt::Horizontal, shell);
    m_splitter->setObjectName(QStringLiteral("mainSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_chatView);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 880});

    auto *layout = new QHBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);
    layout->addWidget(m_splitter, 1);
    setCentralWidget(shell);

    const Config config;
    auto *endpoint = new QLabel(tr("Server: %1").arg(config.serverEndpoint()), this);
    endpoint->setProperty("miniRole", QStringLiteral("caption"));
    statusBar()->addPermanentWidget(endpoint);
    statusBar()->setSizeGripEnabled(false);

    connect(m_rail, &Rail::settingsRequested, this, &MainWindow::openSettings);
    connect(m_rail, &Rail::profileRequested, this, &MainWindow::openProfile);
    connect(m_rail, &Rail::contactsRequested, this, &MainWindow::promptNewChat);
    connect(m_sidebar, &Sidebar::newChatRequested, this, &MainWindow::promptNewChat);
    connect(m_sidebar, &Sidebar::chatSelected, this, &MainWindow::openChatById);
    connect(m_chatView, &ChatView::sendRequested, this, &MainWindow::sendMessage);
    connect(m_chatView, &ChatView::draftChanged, this, [this] {
        QTimer::singleShot(400, this, &MainWindow::saveDraft);
    });
    connect(m_chatView, &ChatView::deleteMessageRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService) {
                    m_messageService->deleteLocal(clientMsgId, [](Result<bool>) {});
                }
            });
    connect(m_chatView, &ChatView::cancelSendRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService) {
                    m_messageService->cancelSend(clientMsgId, [](Result<bool>) {});
                }
            });

    ChatList *list = m_sidebar->chatList();
    connect(list, &ChatList::personSelected, this, &MainWindow::openDirectChatForLogin);
    connect(list, &ChatList::messageSelected, this,
            [this](const QString &chatId, const QString &) { openChatById(chatId); });
    connect(list, &ChatList::pinToggled, this, [this](const QString &chatId, bool pinned) {
        if (m_chatService) {
            m_chatService->setPinned(chatId, pinned, [](Result<bool>) {});
        }
    });
    connect(list, &ChatList::muteToggled, this, [this](const QString &chatId, bool muted) {
        if (m_chatService) {
            m_chatService->setMuted(chatId, muted, [](Result<bool>) {});
        }
    });
    connect(list, &ChatList::markReadRequested, this, [this](const QString &chatId) {
        if (m_chatService) {
            m_chatService->markRead(chatId, [](Result<int>) {});
        }
    });
    connect(list, &ChatList::historyClearRequested, this, [this](const QString &chatId) {
        if (m_chatService) {
            m_chatService->clearHistory(chatId, [](Result<bool>) {});
        }
    });
    connect(list, &ChatList::chatDeleteRequested, this,
            [this](const QString &chatId, bool deleteHistory) {
                if (m_chatService) {
                    m_chatService->deleteChat(chatId, deleteHistory, [](Result<bool>) {});
                }
            });

    if (m_settings) {
        const QString geometry = m_settings->get(kGeometryKey);
        if (!geometry.isEmpty()) {
            restoreGeometry(QByteArray::fromBase64(geometry.toLatin1()));
        }
    }

    setupTray();
    setupShortcuts();
}

void MainWindow::setupShortcuts()
{
    auto bind = [this](const QKeySequence &sequence, auto action) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, action);
    };
    bind(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { promptNewChat(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openSettings(); });
    bind(QKeySequence(Qt::Key_F5), [this] {
        if (m_ws) {
            m_ws->reconnect();
            statusBar()->showMessage(tr("Reconnecting…"), 3000);
        }
    });
    bind(QKeySequence(Qt::Key_F11), [this] {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { quitFromTray(); });
}

void MainWindow::setWsClient(net::WsClient *ws)
{
    if (m_ws == ws) {
        return;
    }
    if (m_ws) {
        disconnect(m_ws, nullptr, this, nullptr);
    }
    m_ws = ws;
    if (!m_ws) {
        return;
    }
    connect(m_ws, &net::WsClient::presence, this,
            [this](const QString &login, bool online, qint64 lastActiveAt) {
                m_sidebar->chatList()->setPresence(login, online, lastActiveAt);
                if (!m_currentChat.peerLogin.isEmpty()
                    && m_currentChat.peerLogin.compare(login, Qt::CaseInsensitive) == 0) {
                    m_chatView->setPeerPresence(online, lastActiveAt);
                }
            });
    connect(m_ws, &net::WsClient::connected, this, &MainWindow::subscribePresence);
    subscribePresence();
}

void MainWindow::subscribePresence()
{
    if (!m_ws) {
        return;
    }
    // The server replaces a device's subscription set on every request, so
    // send the union of everything seen so far instead of only the last chat.
    for (const QString &login : m_sidebar->chatList()->peerLogins()) {
        if (!login.trimmed().isEmpty()) {
            m_presenceLogins.insert(login.trimmed());
        }
    }
    if (!m_currentChat.peerLogin.trimmed().isEmpty()) {
        m_presenceLogins.insert(m_currentChat.peerLogin.trimmed());
    }
    if (m_presenceLogins.isEmpty()) {
        return;
    }
    m_ws->subscribePresence(QStringList(m_presenceLogins.cbegin(), m_presenceLogins.cend()));
}

MainWindow::~MainWindow() = default;

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setObjectName(QStringLiteral("trayIcon"));
    m_tray->setIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    m_tray->setToolTip(tr("MINI Messenger"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->setObjectName(QStringLiteral("trayMenu"));

    m_trayOpen = m_trayMenu->addAction(tr("Open"));
    m_trayOpen->setObjectName(QStringLiteral("trayOpenAction"));
    m_trayDoNotDisturb = m_trayMenu->addAction(tr("Do not disturb"));
    m_trayDoNotDisturb->setObjectName(QStringLiteral("trayDoNotDisturbAction"));
    m_trayDoNotDisturb->setCheckable(true);
    m_trayDoNotDisturb->setChecked(
        m_settings ? m_settings->getBool(kDoNotDisturbKey, false) : false);
    m_traySignOut = m_trayMenu->addAction(tr("Sign out"));
    m_traySignOut->setObjectName(QStringLiteral("traySignOutAction"));
    m_trayExit = m_trayMenu->addAction(tr("Exit"));
    m_trayExit->setObjectName(QStringLiteral("trayExitAction"));

    m_tray->setContextMenu(m_trayMenu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick
                    || reason == QSystemTrayIcon::Trigger) {
                    restoreFromTray();
                }
            });
    connect(m_trayOpen, &QAction::triggered, this, &MainWindow::restoreFromTray);
    connect(m_trayExit, &QAction::triggered, this, &MainWindow::quitFromTray);
    connect(m_traySignOut, &QAction::triggered, this, [this] {
        hide();
        emit signOutRequested();
    });
    connect(m_trayDoNotDisturb, &QAction::toggled, this, [this](bool enabled) {
        if (m_settings) {
            m_settings->setBool(kDoNotDisturbKey, enabled);
            m_settings->flush();
        }
    });

    if (trayAvailable()) {
        m_tray->show();
    }
}

void MainWindow::setChatService(services::ChatService *service)
{
    m_chatService = service;
    m_sidebar->chatList()->setChatService(service);
    refreshChatList();
}

void MainWindow::setContactService(services::ContactService *service)
{
    m_contactService = service;
}

void MainWindow::setSearchService(services::SearchService *service)
{
    m_searchService = service;
    m_sidebar->chatList()->setSearchService(service);
}

void MainWindow::setStorageWorker(storage::StorageWorker *worker)
{
    m_storage = worker;
}

void MainWindow::setMessageService(services::MessageService *service)
{
    if (m_messageService == service) {
        return;
    }
    if (m_messageService) {
        disconnect(m_messageService, nullptr, this, nullptr);
    }
    m_messageService = service;
    connectMessageService();
}

void MainWindow::connectMessageService()
{
    if (!m_messageService) {
        return;
    }
    connect(m_messageService, &services::MessageService::messageAdded, this,
            [this](const Message &message) {
                if (message.chatId == m_currentChat.chatId) {
                    m_chatView->appendMessage(message);
                }
                refreshChatList();
            });
    connect(m_messageService, &services::MessageService::messageUpdated, this,
            [this](const Message &message) {
                if (message.chatId == m_currentChat.chatId) {
                    m_chatView->updateMessage(message);
                }
            });
    connect(m_messageService, &services::MessageService::messageRemoved, this,
            [this](const QString &clientMsgId, const QString &chatId) {
                if (chatId.isEmpty() || chatId == m_currentChat.chatId) {
                    m_chatView->removeMessage(clientMsgId);
                }
            });
    connect(m_messageService, &services::MessageService::errorOccurred, this,
            [this](const QString &code, const QString &) {
                statusBar()->showMessage(tr("Message error: %1").arg(code), 5000);
            });
    connect(m_messageService, &services::MessageService::messageSendFailed, this,
            [this](const QString &, const QString &chatId, const QString &text,
                   const QString &) {
                if (chatId == m_currentChat.chatId && m_chatView->draft().trimmed().isEmpty()) {
                    m_chatView->setDraft(text);
                    statusBar()->showMessage(tr("The message was not sent."), 4000);
                }
            });
    connect(m_messageService, &services::MessageService::pendingChanged, this,
            [this](int pending) {
                if (pending > 0) {
                    statusBar()->showMessage(tr("%1 pending").arg(pending));
                } else {
                    statusBar()->clearMessage();
                }
            });
}

void MainWindow::ensureDiagnosticsService()
{
    if (m_diagnostics) {
        return;
    }
    m_diagnostics = new services::DiagnosticsService(this);
    const Config config;
    m_diagnostics->setEndpoint(config.serverEndpoint());
    if (m_storage) {
        m_diagnostics->setDatabase(m_storage->isOpen(), 0);
    }
}

void MainWindow::openSettings()
{
    ensureDiagnosticsService();
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_settings, this);
        connect(m_settingsDialog, &SettingsDialog::diagnosticsRequested, this,
                &MainWindow::openDiagnostics);
    }
    if (m_storage) {
        m_settingsDialog->setBackupTarget(
            m_storage, QDir(Paths::dataDir()).filePath(QStringLiteral("keys.bin")),
            Paths::backupsDir());
    }
    if (m_diagnostics) {
        m_settingsDialog->setDiagnosticsService(m_diagnostics);
    }
    Log::info(QStringLiteral("ui.settings_opened"));
    m_settingsDialog->exec();
}

void MainWindow::openProfile()
{
    if (!m_profilePanel) {
        m_profilePanel = new ProfilePanel(m_settings, this);
    }
    m_profilePanel->setContactService(m_contactService);
    if (m_contactService) {
        m_contactService->fetchSelf([this](Result<services::SelfProfile> result) {
            if (result.isOk()) {
                m_profilePanel->setProfile(result.value());
            }
        });
    }
    m_profilePanel->exec();
}

void MainWindow::openDiagnostics()
{
    ensureDiagnosticsService();
    DiagnosticsDialog dialog(m_diagnostics, this);
    dialog.setLogDirectory(Paths::logsDir());
    dialog.exec();
}

void MainWindow::promptNewChat()
{
    bool accepted = false;
    const QString login = QInputDialog::getText(this, tr("New chat"), tr("Exact login"),
                                                QLineEdit::Normal, QString(), &accepted)
                              .trimmed();
    if (accepted && !login.isEmpty()) {
        openDirectChatForLogin(login);
    }
}

void MainWindow::openDirectChatForLogin(const QString &login)
{
    if (!m_chatService) {
        statusBar()->showMessage(tr("Chats are not available yet."), 4000);
        return;
    }
    auto openNow = [this](const QString &peerLogin) {
        m_chatService->openDirectChat(peerLogin, [this](Result<Chat> result) {
            if (result.isError()) {
                statusBar()->showMessage(
                    tr("Could not open the chat: %1").arg(result.error()), 5000);
                return;
            }
            statusBar()->clearMessage();
            openChat(result.value());
        });
    };

    const QString trimmed = login.trimmed();
    if (m_contactService && m_contactService->hasLookup()) {
        m_contactService->resolve(trimmed, [openNow, this](Result<services::PeerProfile> result) {
            if (result.isError()) {
                statusBar()->showMessage(
                    tr("Could not open the chat: %1").arg(result.error()), 5000);
                return;
            }
            openNow(result.value().login);
        });
        return;
    }
    openNow(trimmed);
}

void MainWindow::openChatById(const QString &chatId)
{
    Chat chat = m_sidebar->chatList()->chatById(chatId);
    if (!chat.chatId.isEmpty()) {
        openChat(chat);
        return;
    }
    if (!m_chatService) {
        return;
    }
    m_chatService->listChats([this, chatId](Result<QVector<Chat>> result) {
        if (result.isError()) {
            return;
        }
        for (const Chat &candidate : result.value()) {
            if (candidate.chatId == chatId) {
                openChat(candidate);
                return;
            }
        }
    });
}

void MainWindow::openChat(const Chat &chat)
{
    m_currentChat = chat;
    m_chatView->setChat(chat);
    m_chatView->setMessages({});

    if (m_messageService) {
        const QString chatId = chat.chatId;
        m_messageService->loadHistory(chat.chatId, MessageCursor{}, 200,
                                      [this, chatId](Result<QVector<Message>> result) {
            if (result.isOk() && m_currentChat.chatId == chatId) {
                m_chatView->setMessages(result.value());
            }
        });
    }
    if (m_chatService) {
        const QString chatId = chat.chatId;
        m_chatService->loadDraft(chat.chatId, [this, chatId](Result<QString> result) {
            if (result.isOk() && m_currentChat.chatId == chatId) {
                m_chatView->setDraft(result.value());
            }
        });
        m_chatService->markRead(chat.chatId, [](Result<int>) {});
    }
    if (m_ws) {
        if (!chat.peerLogin.trimmed().isEmpty()) {
            m_presenceLogins.insert(chat.peerLogin.trimmed());
        }
        subscribePresence();
    }
    if (m_contactService && m_contactService->hasLookup()) {
        const QString chatId = chat.chatId;
        m_contactService->resolve(chat.peerLogin, [this, chatId](
                                                    Result<services::PeerProfile> result) {
            if (!result.isOk() || m_currentChat.chatId != chatId) {
                return;
            }
            const services::PeerProfile &peer = result.value();
            m_sidebar->chatList()->setPresence(peer.login, peer.online, peer.lastActiveAt);
            m_chatView->setPeerPresence(peer.online, peer.lastActiveAt);
        });
    }
    statusBar()->clearMessage();
}

void MainWindow::sendMessage(const QString &text)
{
    if (!m_messageService || m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Messaging is not available yet."), 4000);
        m_chatView->setDraft(text);
        return;
    }
    const QString clientMsgId =
        m_messageService->sendText(m_currentChat.chatId, text,
                                   m_chatView->replyToClientMsgId());
    if (clientMsgId.isEmpty()) {
        // Refused before it reached the outbox: give the text back.
        m_chatView->setDraft(text);
        statusBar()->showMessage(tr("The message was not sent."), 4000);
    }
}

void MainWindow::saveDraft()
{
    if (!m_chatService || m_currentChat.chatId.isEmpty()) {
        return;
    }
    const QString text = m_chatView->draft();
    if (text.trimmed().isEmpty()) {
        m_chatService->clearDraft(m_currentChat.chatId, [](Result<bool>) {});
        return;
    }
    m_chatService->saveDraft(m_currentChat.chatId, text, [](Result<bool>) {});
}

void MainWindow::refreshChatList()
{
    if (!m_chatService) {
        return;
    }
    m_chatService->listChats([this](Result<QVector<Chat>> result) {
        if (result.isOk()) {
            m_sidebar->chatList()->setChats(result.value());
            subscribePresence();
        }
    });
}

void MainWindow::saveGeometryToSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->set(kGeometryKey,
                    QString::fromLatin1(saveGeometry().toBase64()));
    m_settings->flush();
}

bool MainWindow::closeToTrayEnabled() const
{
    return m_settings ? m_settings->getBool(kCloseToTrayKey, true) : true;
}

void MainWindow::setCloseToTray(bool enabled)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(kCloseToTrayKey, enabled);
    m_settings->flush();
}

bool MainWindow::doNotDisturb() const
{
    return m_trayDoNotDisturb && m_trayDoNotDisturb->isChecked();
}

void MainWindow::setTrayAvailableForTesting(bool available)
{
    m_trayAvailableOverride = available ? 1 : 0;
}

bool MainWindow::trayAvailable() const
{
    if (m_trayAvailableOverride >= 0) {
        return m_trayAvailableOverride == 1;
    }
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void MainWindow::restoreFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::quitFromTray()
{
    m_quitting = true;
    saveGeometryToSettings();
    if (m_tray) {
        m_tray->hide();
    }
    QCoreApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryToSettings();
    if (!m_quitting && closeToTrayEnabled() && trayAvailable()) {
        hide();
        event->ignore();
        return;
    }
    if (m_tray) {
        m_tray->hide();
    }
    QMainWindow::closeEvent(event);
}

} // namespace mini
