#include "ui/MainWindow.h"

#include "app/Config.h"
#include "app/Paths.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "domain/Channel.h"
#include "domain/Group.h"
#include "domain/Message.h"
#include "services/ChannelService.h"
#include "services/ChatService.h"
#include "services/ContactService.h"
#include "services/DiagnosticsService.h"
#include "services/GifCatalog.h"
#include "net/WsClient.h"
#include "services/GroupService.h"
#include "services/MediaCache.h"
#include "services/MediaService.h"
#include "services/MessageService.h"
#include "services/SearchService.h"
#include "services/StickerService.h"
#include "ui/AlbumSendDialog.h"
#include "storage/StorageWorker.h"
#include "ui/ChatList.h"
#include "ui/ChatListModel.h"
#include "ui/ChatView.h"
#include "ui/ChannelInfoPanel.h"
#include "ui/Composer.h"
#include "ui/DiagnosticsDialog.h"
#include "ui/ForwardDialog.h"
#include "ui/GroupInfoPanel.h"
#include "ui/MediaSendDialog.h"
#include "ui/MediaViewer.h"
#include "ui/MessageBubble.h"
#include "ui/NewChannelDialog.h"
#include "ui/NewGroupDialog.h"
#include "ui/ProfilePanel.h"
#include "ui/Rail.h"
#include "ui/SettingsDialog.h"
#include "ui/Sidebar.h"

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

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
    connect(m_sidebar, &Sidebar::newGroupRequested, this, &MainWindow::openNewGroupDialog);
    connect(m_sidebar, &Sidebar::newChannelRequested, this,
            &MainWindow::openNewChannelDialog);
    connect(m_sidebar, &Sidebar::joinGroupRequested, this, &MainWindow::promptJoinGroup);
    connect(m_sidebar, &Sidebar::joinChannelRequested, this,
            &MainWindow::promptJoinChannel);
    connect(m_sidebar, &Sidebar::chatSelected, this, &MainWindow::openChatById);
    connect(m_chatView, &ChatView::sendRequested, this, &MainWindow::sendMessage);
    connect(m_chatView, &ChatView::attachRequested, this, &MainWindow::attachFiles);
    connect(m_chatView, &ChatView::filesDropped, this, &MainWindow::handleFiles);
    connect(m_chatView, &ChatView::stickerRequested, this, &MainWindow::sendSticker);
    connect(m_chatView, &ChatView::gifRequested, this, &MainWindow::sendGif);
    connect(m_chatView, &ChatView::stickerInstallRequested, this,
            &MainWindow::installStickerPack);
    connect(m_chatView, &ChatView::stickerRemoveRequested, this,
            &MainWindow::removeStickerPack);
    connect(m_chatView, &ChatView::recentEmojiChanged, this,
            &MainWindow::saveEmojiRecents);
    connect(m_chatView, &ChatView::mediaActivated, this, &MainWindow::openMedia);
    if (m_settings) {
        const QString stored = m_settings->get(QStringLiteral("emoji/recent"));
        m_chatView->composer()->setRecentEmoji(
            stored.split(QLatin1Char('\x1f'), Qt::SkipEmptyParts));
    }
    connect(m_chatView, &ChatView::chatInfoRequested, this, [this] {
        const QString channelId = channel_ids::channelIdFromChat(m_currentChat.chatId);
        if (!channelId.isEmpty()) {
            openChannelInfo(channelId);
            return;
        }
        const QString groupId = group_ids::groupIdFromChat(m_currentChat.chatId);
        if (!groupId.isEmpty()) {
            openGroupInfo(groupId);
        }
    });
    connect(m_chatView, &ChatView::draftChanged, this, [this] {
        QTimer::singleShot(400, this, &MainWindow::saveDraft);
    });
    connect(m_chatView, &ChatView::deleteMessageRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService) {
                    m_messageService->deleteLocal(clientMsgId, [](Result<bool>) {});
                }
            });
    connect(m_chatView, &ChatView::deleteForEveryoneRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService) {
                    const bool admin = m_chatView->mentionAllAllowed();
                    m_messageService->deleteForEveryone(
                        clientMsgId, admin, [](Result<bool>) {});
                    refreshPins();
                }
            });
    connect(m_chatView, &ChatView::editRequested, this,
            [this](const QString &clientMsgId, const QString &text) {
                if (m_messageService) {
                    m_messageService->editMessage(clientMsgId, text,
                                                  [](Result<bool>) {});
                }
            });
    connect(m_chatView, &ChatView::forwardRequested, this,
            [this](const QString &clientMsgId) {
                openForwardDialog({clientMsgId});
            });
    connect(m_chatView, &ChatView::forwardSelectedRequested, this,
            &MainWindow::openForwardDialog);
    connect(m_chatView, &ChatView::copySelectedRequested, this,
            [this](const QStringList &) { statusBar()->showMessage(tr("Copied"), 2000); });
    connect(m_chatView, &ChatView::pinRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService && !m_currentChat.chatId.isEmpty()) {
                    m_messageService->pinMessage(
                        m_currentChat.chatId, clientMsgId, [this](Result<bool> result) {
                            if (result.isError()) {
                                statusBar()->showMessage(
                                    result.error() == QLatin1String("pin_limit")
                                        ? tr("Up to 20 pinned messages per chat")
                                        : tr("Could not pin: %1").arg(result.error()),
                                    4000);
                            }
                            refreshPins();
                        });
                }
            });
    connect(m_chatView, &ChatView::unpinRequested, this,
            [this](const QString &clientMsgId) {
                if (m_messageService && !m_currentChat.chatId.isEmpty()) {
                    m_messageService->unpinMessage(m_currentChat.chatId, clientMsgId,
                                                   [this](Result<bool>) { refreshPins(); });
                }
            });
    connect(m_chatView, &ChatView::mentionDenied, this, [this](const QString &) {
        statusBar()->showMessage(tr("@all is available to administrators only"), 4000);
    });
    connect(m_chatView, &ChatView::deleteSelectedRequested, this,
            [this](const QStringList &ids) {
                for (const QString &id : ids) {
                    if (m_messageService) {
                        m_messageService->deleteForEveryone(
                            id, m_chatView->mentionAllAllowed(), [](Result<bool>) {});
                    }
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
            [this](const QString &chatId, const QString &clientMsgId) {
                jumpToMessage(chatId, clientMsgId);
            });
    connect(list, &ChatList::pinToggled, this, [this](const QString &chatId, bool pinned) {
        if (m_chatService) {
            m_chatService->setPinned(chatId, pinned, [](Result<bool>) {});
        }
    });
    connect(list, &ChatList::muteUntilRequested, this,
            [this](const QString &chatId, qint64 untilMs) {
                if (m_chatService) {
                    m_chatService->setMutedUntil(chatId, untilMs, [](Result<bool>) {});
                }
            });
    connect(list, &ChatList::archiveToggled, this,
            [this](const QString &chatId, bool archived) {
                if (m_chatService) {
                    m_chatService->setArchived(chatId, archived, [](Result<bool>) {});
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
    bind(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { openNewGroupDialog(); });
    bind(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
         [this] { openNewChannelDialog(); });
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

void MainWindow::setGroupService(services::GroupService *service)
{
    if (m_groupService == service) {
        return;
    }
    if (m_groupService) {
        disconnect(m_groupService, nullptr, this, nullptr);
    }
    m_groupService = service;
    if (!m_groupService) {
        return;
    }
    connect(m_groupService, &services::GroupService::groupsChanged, this,
            &MainWindow::refreshChatList);
    connect(m_groupService, &services::GroupService::errorOccurred, this,
            [this](const QString &code, const QString &) {
                statusBar()->showMessage(tr("Group error: %1").arg(code), 5000);
            });
}

void MainWindow::setChannelService(services::ChannelService *service)
{
    if (m_channelService == service) {
        return;
    }
    if (m_channelService) {
        disconnect(m_channelService, nullptr, this, nullptr);
    }
    m_channelService = service;
    if (!m_channelService) {
        return;
    }
    connect(m_channelService, &services::ChannelService::channelsChanged, this,
            &MainWindow::refreshChatList);
    connect(m_channelService, &services::ChannelService::errorOccurred, this,
            [this](const QString &code, const QString &) {
                statusBar()->showMessage(tr("Channel error: %1").arg(code), 5000);
            });
}

void MainWindow::setSearchService(services::SearchService *service)
{
    m_searchService = service;
    m_sidebar->chatList()->setSearchService(service);
}

void MainWindow::setStickerService(services::StickerService *service)
{
    if (m_stickers == service) {
        return;
    }
    m_stickers = service;
    m_chatView->setStickerService(service);
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

void MainWindow::setMediaService(services::MediaService *service)
{
    if (m_media == service) {
        return;
    }
    if (m_media) {
        disconnect(m_media, nullptr, this, nullptr);
    }
    m_media = service;
    m_chatView->setMediaService(service);
    if (!service) {
        return;
    }
    connect(service, &services::MediaService::uploadStarted, this,
            [this](const QString &mediaId) {
                if (m_pendingUpload.clientMsgId.isEmpty()) {
                    return;
                }
                m_pendingUpload.mediaId = mediaId;
                m_chatView->updateMessage(m_pendingUpload);
            });
    connect(service, &services::MediaService::uploadProgress, this,
            [this](const QString &, qint64 sent, qint64 total) {
                const int percent = total > 0 ? static_cast<int>((sent * 100) / total) : 0;
                statusBar()->showMessage(tr("Uploading attachment: %1%").arg(percent));
            });
    connect(service, &services::MediaService::uploadFinished, this,
            [this](const QString &, bool ok, const QString &error) {
                if (ok) {
                    statusBar()->showMessage(tr("Attachment uploaded"), 3000);
                } else {
                    statusBar()->showMessage(
                        tr("Attachment upload failed: %1").arg(error), 5000);
                }
            });
    connect(service, &services::MediaService::downloadProgress, this,
            [this](const QString &, qint64 received, qint64 total) {
                const int percent = total > 0 ? static_cast<int>((received * 100) / total) : 0;
                statusBar()->showMessage(tr("Downloading: %1%").arg(percent));
            });
    connect(service, &services::MediaService::errorOccurred, this,
            [this](const QString &code, const QString &detail) {
                if (!detail.isEmpty()) {
                    statusBar()->showMessage(tr("Media: %1 — %2").arg(code, detail), 5000);
                }
            });
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
    connect(m_messageService, &services::MessageService::pinsChanged, this,
            [this](const QString &chatId) {
                if (chatId.isEmpty() || chatId == m_currentChat.chatId) {
                    refreshPins();
                }
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

void MainWindow::openNewGroupDialog()
{
    if (!m_groupService) {
        statusBar()->showMessage(tr("Groups are not available yet."), 4000);
        return;
    }
    NewGroupDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const services::GroupDraft draft = dialog.draft();
    m_groupService->createGroup(draft, [this](Result<Group> result) {
        if (result.isError()) {
            statusBar()->showMessage(tr("Could not create the group: %1").arg(result.error()),
                                     6000);
            return;
        }
        const Group &group = result.value();
        Chat chat;
        chat.chatId = group_ids::chatId(group.groupId);
        chat.kind = QStringLiteral("group");
        chat.title = group.title.isEmpty() ? group.groupId : group.title;
        chat.updatedAt = group.updatedAt;
        refreshChatList();
        openChat(chat);
    });
}

void MainWindow::openGroupInfo(const QString &groupId)
{
    if (groupId.isEmpty()) {
        return;
    }
    auto *panel = new GroupInfoPanel(this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setGroupService(m_groupService);
    connect(panel, &GroupInfoPanel::groupLeft, this, [this](const QString &leftGroupId) {
        if (m_currentChat.chatId == group_ids::chatId(leftGroupId)) {
            m_currentChat = Chat{};
            m_chatView->clearChat();
        }
        refreshChatList();
    });
    connect(panel, &GroupInfoPanel::groupChanged, this, [this](const QString &) {
        refreshChatList();
    });
    Group stub;
    stub.groupId = groupId;
    panel->setGroup(stub);
    panel->refresh();
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void MainWindow::promptJoinGroup()
{
    if (!m_groupService) {
        statusBar()->showMessage(tr("Groups are not available yet."), 4000);
        return;
    }
    bool accepted = false;
    QString token = QInputDialog::getText(this, tr("Join group"),
                                          tr("Invite link or token"), QLineEdit::Normal,
                                          QString(), &accepted)
                        .trimmed();
    if (!accepted || token.isEmpty()) {
        return;
    }
    if (token.startsWith(QLatin1String("mini://g/"))) {
        token = token.mid(9);
    } else if (token.startsWith(QLatin1String("mini://join/"))) {
        token = token.mid(12);
    }
    m_groupService->joinByToken(token, [this](Result<Group> result) {
        if (result.isError()) {
            statusBar()->showMessage(
                result.error() == QLatin1String("join_pending")
                    ? tr("The join request was sent for approval.")
                    : tr("Could not join the group: %1").arg(result.error()),
                6000);
            return;
        }
        const Group &group = result.value();
        Chat chat;
        chat.chatId = group_ids::chatId(group.groupId);
        chat.kind = QStringLiteral("group");
        chat.title = group.title.isEmpty() ? group.groupId : group.title;
        chat.updatedAt = group.updatedAt;
        refreshChatList();
        openChat(chat);
    });
}

void MainWindow::openNewChannelDialog()
{
    if (!m_channelService) {
        statusBar()->showMessage(tr("Channels are not available yet."), 4000);
        return;
    }
    NewChannelDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const services::ChannelDraft draft = dialog.draft();
    m_channelService->createChannel(draft, [this](Result<Channel> result) {
        if (result.isError()) {
            statusBar()->showMessage(
                tr("Could not create the channel: %1").arg(result.error()), 6000);
            return;
        }
        const Channel &channel = result.value();
        Chat chat;
        chat.chatId = channel_ids::chatId(channel.channelId);
        chat.kind = QStringLiteral("channel");
        chat.title = channel.title.isEmpty() ? channel.channelId : channel.title;
        chat.updatedAt = channel.updatedAt;
        refreshChatList();
        openChat(chat);
    });
}

void MainWindow::openChannelInfo(const QString &channelId)
{
    if (channelId.isEmpty()) {
        return;
    }
    auto *panel = new ChannelInfoPanel(this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setChannelService(m_channelService);
    connect(panel, &ChannelInfoPanel::channelLeft, this, [this](const QString &leftChannelId) {
        if (m_currentChat.chatId == channel_ids::chatId(leftChannelId)) {
            m_currentChat = Chat{};
            m_chatView->clearChat();
        }
        refreshChatList();
    });
    connect(panel, &ChannelInfoPanel::channelDeleted, this,
            [this](const QString &deletedChannelId) {
                if (m_currentChat.chatId == channel_ids::chatId(deletedChannelId)) {
                    m_currentChat = Chat{};
                    m_chatView->clearChat();
                }
                refreshChatList();
            });
    connect(panel, &ChannelInfoPanel::channelChanged, this, [this](const QString &) {
        refreshChatList();
    });
    Channel stub;
    stub.channelId = channelId;
    panel->setChannel(stub);
    panel->refresh();
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void MainWindow::promptJoinChannel()
{
    if (!m_channelService) {
        statusBar()->showMessage(tr("Channels are not available yet."), 4000);
        return;
    }
    bool accepted = false;
    QString value = QInputDialog::getText(this, tr("Join channel"),
                                          tr("Invite link or token"), QLineEdit::Normal,
                                          QString(), &accepted)
                        .trimmed();
    if (!accepted || value.isEmpty()) {
        return;
    }
    QString channelId;
    QString token;
    if (value.startsWith(QLatin1String("mini://c/"))) {
        const QString tail = value.mid(9);
        const QStringList parts = tail.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            channelId = parts.at(0);
        }
        if (parts.size() > 1) {
            token = parts.at(1);
        }
    } else if (value.startsWith(QLatin1String("c_"))) {
        channelId = value.section(QLatin1Char('/'), 0, 0);
        token = value.section(QLatin1Char('/'), 1, 1);
    }
    if (channelId.isEmpty()) {
        statusBar()->showMessage(tr("The invite link has no channel id."), 6000);
        return;
    }
    m_channelService->subscribe(channelId, token, [this](Result<Channel> result) {
        if (result.isError()) {
            statusBar()->showMessage(
                tr("Could not subscribe: %1").arg(result.error()), 6000);
            return;
        }
        const Channel &channel = result.value();
        Chat chat;
        chat.chatId = channel_ids::chatId(channel.channelId);
        chat.kind = QStringLiteral("channel");
        chat.title = channel.title.isEmpty() ? channel.channelId : channel.title;
        chat.updatedAt = channel.updatedAt;
        refreshChatList();
        openChat(chat);
    });
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
    refreshPins();

    if (group_ids::isGroupChat(chat.chatId) && m_groupService) {
        const QString chatId = chat.chatId;
        const QString groupId = group_ids::groupIdFromChat(chat.chatId);
        m_groupService->loadGroup(groupId, [this, chatId](Result<Group> result) {
            if (result.isOk() && m_currentChat.chatId == chatId) {
                m_chatView->setMentionAllAllowed(result.value().myRole
                                                 == GroupRole::Admin
                                                 || result.value().myRole
                                                     == GroupRole::Owner);
            }
        });
    } else if (channel_ids::isChannelChat(chat.chatId) && m_channelService) {
        const QString chatId = chat.chatId;
        const QString channelId = channel_ids::channelIdFromChat(chat.chatId);
        m_channelService->loadChannel(channelId, [this, chatId](Result<Channel> result) {
            if (result.isOk() && m_currentChat.chatId == chatId) {
                m_chatView->setMentionAllAllowed(result.value().myRole
                                                 == ChannelRole::Admin
                                                 || result.value().myRole
                                                     == ChannelRole::Owner);
            }
        });
    } else {
        m_chatView->setMentionAllAllowed(false);
    }

    if (channel_ids::isChannelChat(chat.chatId)) {
        // §43.2: subscribers read, publishers post. Keep the composer off
        // until the channel role has been loaded.
        m_chatView->setComposerEnabled(false);
        if (m_channelService) {
            const QString channelId = channel_ids::channelIdFromChat(chat.chatId);
            const QString chatId = chat.chatId;
            m_channelService->loadChannel(
                channelId, [this, chatId](Result<Channel> result) {
                    if (!result.isOk() || m_currentChat.chatId != chatId) {
                        return;
                    }
                    const Channel &channel = result.value();
                    m_chatView->setChatSubtitle(
                        tr("%n subscriber(s)", "", channel.subscriberCount));
                    m_chatView->setComposerEnabled(channel.canPublish());
                });
        }
    }

    if (m_messageService) {
        const QString chatId = chat.chatId;
        m_messageService->loadHistory(chat.chatId, MessageCursor{}, 200,
                                      [this, chatId](Result<QVector<Message>> result) {
            if (result.isOk() && m_currentChat.chatId == chatId) {
                const QVector<Message> messages = result.value();
                m_chatView->setMessages(messages);
                if (!m_pendingJumpId.isEmpty()) {
                    const QString target = m_pendingJumpId;
                    m_pendingJumpId.clear();
                    m_chatView->jumpToMessage(target);
                }
                // §43.5: a post counts as viewed after it is on screen for a
                // second; the server deduplicates (channel, post, user).
                if (channel_ids::isChannelChat(chatId) && m_channelService) {
                    const QString channelId =
                        channel_ids::channelIdFromChat(chatId);
                    QTimer::singleShot(1000, this, [this, chatId, channelId, messages] {
                        if (m_currentChat.chatId != chatId) {
                            return;
                        }
                        for (const Message &message : messages) {
                            if (message.outgoing) {
                                continue;
                            }
                            m_channelService->recordView(
                                channelId, message.clientMsgId, [](Result<int>) {});
                        }
                    });
                }
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
    if (m_contactService && m_contactService->hasLookup()
        && !chat.peerLogin.trimmed().isEmpty()) {
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

void MainWindow::attachFiles()
{
    if (!m_media || m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Select a chat first."), 3000);
        return;
    }
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Attach a file"), QString(), tr("All files (*.*)"));
    if (!paths.isEmpty()) {
        handleFiles(paths);
    }
}

void MainWindow::handleFiles(const QStringList &paths)
{
    if (!m_media || m_messageService == nullptr) {
        statusBar()->showMessage(tr("Attachments are unavailable."), 3000);
        return;
    }
    if (m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Select a chat first."), 3000);
        return;
    }
    QStringList files;
    for (const QString &path : paths) {
        if (QFileInfo(path).isFile()) {
            files.append(path);
        }
    }
    if (files.isEmpty()) {
        return;
    }
    if (files.size() >= 2) {
        handleAlbum(files);
        return;
    }
    const bool wasEmpty = m_fileQueue.isEmpty();
    m_fileQueue.append(files);
    if (wasEmpty) {
        processNextFile();
    }
}

void MainWindow::handleAlbum(const QStringList &paths)
{
    if (!m_media || !m_messageService || m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Select a chat first."), 3000);
        return;
    }
    const QString chatId = m_currentChat.chatId;
    const QString replyTo = m_chatView->replyToClientMsgId();
    QStringList files = paths;
    if (files.size() > 10) {
        files = files.mid(0, 10);
        statusBar()->showMessage(tr("An album holds at most 10 items."), 4000);
    }

    struct State
    {
        QVector<services::MediaPreparation> items;
        int index = 0;
        QString error;
    };
    auto state = std::make_shared<State>();
    auto next = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakNext = next;
    *next = [this, files, chatId, replyTo, state, weakNext] {
        if (m_currentChat.chatId != chatId) {
            return;
        }
        if (!state->error.isEmpty()) {
            statusBar()->showMessage(
                tr("Album could not be prepared: %1").arg(state->error), 5000);
            return;
        }
        if (state->index >= files.size()) {
            if (state->items.isEmpty()) {
                return;
            }
            QPixmap preview;
            const services::MediaPreparation &first = state->items.first();
            preview.loadFromData(first.preview1280.isEmpty() ? first.thumb320
                                                              : first.preview1280);
            AlbumSendDialog dialog(state->items.size(), preview, this);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }
            uploadAlbum(chatId, state->items, dialog.caption(), replyTo);
            return;
        }
        const QString path = files.at(state->index);
        m_media->prepareFile(
            path, [this, state, weakNext](Result<services::MediaPreparation> prepared) {
                if (prepared.isError()) {
                    state->error = prepared.error();
                } else {
                    state->items.append(prepared.value());
                }
                ++state->index;
                if (auto nextFn = weakNext.lock()) {
                    (*nextFn)();
                }
            });
    };
    (*next)();
}

void MainWindow::uploadAlbum(const QString &chatId,
                             const QVector<services::MediaPreparation> &items,
                             const QString &caption, const QString &replyTo)
{
    if (!m_media || !m_messageService) {
        return;
    }
    struct State
    {
        QStringList ids;
        int index = 0;
        QString error;
    };
    auto state = std::make_shared<State>();
    auto next = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakNext = next;
    *next = [this, chatId, items, caption, replyTo, state, weakNext] {
        if (!state->error.isEmpty()) {
            statusBar()->showMessage(
                tr("Album was not sent: %1").arg(state->error), 5000);
            return;
        }
        if (state->index >= items.size()) {
            if (state->ids.size() != items.size() || items.isEmpty()) {
                statusBar()->showMessage(tr("Album was not sent."), 4000);
                return;
            }
            if (m_currentChat.chatId != chatId || !m_messageService) {
                return;
            }
            const QString clientId =
                m_messageService->sendAlbum(chatId, state->ids, caption, replyTo);
            if (clientId.isEmpty()) {
                statusBar()->showMessage(tr("The message was not sent."), 4000);
            } else {
                m_chatView->clearReply();
                statusBar()->showMessage(tr("Album sent."), 2500);
            }
            return;
        }
        statusBar()->showMessage(
            tr("Uploading album %1/%2…").arg(state->index + 1).arg(items.size()));
        const services::MediaPreparation prep = items.at(state->index);
        m_media->upload(
            chatId, prep,
            [this, state, weakNext](Result<QString> uploaded) {
                if (uploaded.isError()) {
                    state->error = uploaded.error();
                } else {
                    state->ids.append(uploaded.value());
                }
                ++state->index;
                if (auto nextFn = weakNext.lock()) {
                    (*nextFn)();
                }
            });
    };
    (*next)();
}

void MainWindow::sendSticker(const QString &packId, const QString &stickerId)
{
    if (!m_messageService || m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Select a chat first."), 3000);
        return;
    }
    QString title = stickerId;
    if (m_stickers) {
        if (const auto pack = m_stickers->pack(packId); pack.has_value()) {
            for (const services::StickerItem &sticker : pack->stickers) {
                if (sticker.id == stickerId) {
                    title = sticker.title;
                    break;
                }
            }
        }
    }
    const QString clientId = m_messageService->sendSticker(
        m_currentChat.chatId, packId, stickerId, title,
        m_chatView->replyToClientMsgId());
    if (clientId.isEmpty()) {
        statusBar()->showMessage(tr("The sticker was not sent."), 4000);
    } else {
        m_chatView->clearReply();
    }
}

void MainWindow::sendGif(const QString &gifId)
{
    if (!m_messageService || m_currentChat.chatId.isEmpty()) {
        statusBar()->showMessage(tr("Select a chat first."), 3000);
        return;
    }
    const QString clientId = m_messageService->sendGif(
        m_currentChat.chatId, gifId, services::GifCatalog::instance().title(gifId),
        m_chatView->replyToClientMsgId());
    if (clientId.isEmpty()) {
        statusBar()->showMessage(tr("The GIF was not sent."), 4000);
    } else {
        m_chatView->clearReply();
    }
}

void MainWindow::installStickerPack()
{
    if (!m_stickers) {
        statusBar()->showMessage(tr("Stickers are unavailable."), 3000);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Install sticker pack"), QString(),
        tr("MINI sticker pack (*.ministicker)"));
    if (path.isEmpty()) {
        return;
    }
    QString packId;
    const Result<bool> result = m_stickers->install(path, &packId);
    if (result.isError()) {
        statusBar()->showMessage(
            tr("The pack could not be installed: %1").arg(result.error()), 6000);
        return;
    }
    statusBar()->showMessage(tr("Sticker pack installed: %1").arg(packId), 4000);
}

void MainWindow::removeStickerPack(const QString &packId)
{
    if (!m_stickers) {
        return;
    }
    const Result<bool> result = m_stickers->remove(packId);
    if (result.isError()) {
        statusBar()->showMessage(
            tr("The pack could not be removed: %1").arg(result.error()), 5000);
        return;
    }
    statusBar()->showMessage(tr("Sticker pack removed."), 3000);
}

void MainWindow::saveEmojiRecents(const QStringList &characters)
{
    if (!m_settings) {
        return;
    }
    m_settings->set(QStringLiteral("emoji/recent"),
                    characters.join(QLatin1Char('\x1f')));
    m_settings->flush();
}

void MainWindow::processNextFile()
{
    if (m_fileQueue.isEmpty() || !m_media || !m_messageService
        || m_currentChat.chatId.isEmpty()) {
        return;
    }
    const QString path = m_fileQueue.takeFirst();
    const QString chatId = m_currentChat.chatId;
    m_media->prepareFile(path, [this, path, chatId](Result<services::MediaPreparation> result) {
        if (m_currentChat.chatId != chatId) {
            processNextFile();
            return;
        }
        if (result.isError()) {
            statusBar()->showMessage(
                tr("File could not be prepared: %1").arg(result.error()), 5000);
            processNextFile();
            return;
        }
        const services::MediaPreparation prep = result.value();
        QPixmap preview;
        preview.loadFromData(prep.preview1280.isEmpty() ? prep.thumb320 : prep.preview1280);
        MediaSendDialog dialog(prep, preview, this);
        if (dialog.exec() != QDialog::Accepted) {
            processNextFile();
            return;
        }
        sendPreparedFile(prep, path, dialog.caption(), dialog.sendAsFile(),
                         m_chatView->replyToClientMsgId());
    });
}

void MainWindow::sendPreparedFile(const services::MediaPreparation &prep,
                                  const QString &path, const QString &caption,
                                  bool asFile, const QString &replyTo)
{
    if (!m_media || !m_messageService) {
        processNextFile();
        return;
    }
    const QString chatId = m_currentChat.chatId;
    auto uploadPrepared = [this, caption, replyTo, chatId](const services::MediaPreparation &ready) {
        statusBar()->showMessage(tr("Uploading attachment…"));
        // Optimistic bubble: it shows the thumbnail and the progress bar
        // before the message exists in the database (§40.3, §11.5 #84).
        Message placeholder;
        placeholder.clientMsgId = QStringLiteral("upload-%1")
                                      .arg(QDateTime::currentMSecsSinceEpoch());
        placeholder.chatId = chatId;
        placeholder.kind = ready.kind;
        placeholder.body = caption;
        placeholder.mediaId = QStringLiteral("pending-") + placeholder.clientMsgId;
        placeholder.mediaName = ready.name;
        placeholder.mediaSize = ready.size;
        placeholder.outgoing = true;
        placeholder.state = MessageState::Queued;
        placeholder.localTs = QDateTime::currentMSecsSinceEpoch();
        const QString placeholderId = placeholder.clientMsgId;
        m_pendingUpload = placeholder;
        m_chatView->appendMessage(placeholder);

        m_media->upload(chatId, ready, [this, caption, replyTo, chatId,
                                        placeholderId](Result<QString> uploaded) {
            if (m_pendingUpload.clientMsgId == placeholderId) {
                m_chatView->removeMessage(placeholderId);
                m_pendingUpload = Message();
            }
            if (uploaded.isError()) {
                statusBar()->showMessage(
                    tr("Attachment was not sent: %1").arg(uploaded.error()), 5000);
                processNextFile();
                return;
            }
            if (m_messageService && m_currentChat.chatId == chatId) {
                const QString clientId = m_messageService->sendMedia(
                    chatId, uploaded.value(), caption, replyTo);
                if (clientId.isEmpty()) {
                    statusBar()->showMessage(tr("The message was not sent."), 4000);
                } else {
                    m_chatView->clearReply();
                }
            }
            processNextFile();
        });
    };

    if (asFile && prep.kind == MediaKind::Photo) {
        m_media->prepareFile(
            path,
            [this, path, caption, replyTo, uploadPrepared](Result<services::MediaPreparation> r) {
                if (r.isError()) {
                    statusBar()->showMessage(
                        tr("File could not be prepared: %1").arg(r.error()), 5000);
                    processNextFile();
                    return;
                }
                uploadPrepared(r.value());
            },
            true);
        return;
    }
    uploadPrepared(prep);
}

void MainWindow::openMedia(const QString &clientMsgId)
{
    if (!m_media || !m_messageService) {
        return;
    }
    auto *model = m_chatView->messageModel();
    const int row = model->indexOf(clientMsgId);
    if (row < 0) {
        return;
    }
    const Message message = model->messageAt(row);
    if (message.mediaId.isEmpty()) {
        return;
    }
    if (message.kind == MediaKind::Album) {
        showAlbumViewer(clientMsgId);
        return;
    }

    if (message.kind == MediaKind::File) {
        statusBar()->showMessage(tr("Downloading…"));
        m_media->download(message.mediaId, [this, message](Result<QString> result) {
            if (result.isError()) {
                statusBar()->showMessage(
                    tr("Download failed: %1").arg(result.error()), 5000);
                return;
            }
            m_media->cache()->ensureDirs();
            const QString name = QFileInfo(message.mediaName).fileName();
            const QString target = QDir(m_media->cache()->downloadsDir())
                                       .filePath(name.isEmpty()
                                                     ? message.mediaId
                                                     : name);
            QFile::remove(target);
            if (!QFile::copy(result.value(), target)) {
                statusBar()->showMessage(tr("Could not save the file."), 4000);
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        });
        return;
    }

    statusBar()->showMessage(tr("Downloading…"));
    m_media->download(message.mediaId, [this, message](Result<QString> result) {
        if (result.isError()) {
            statusBar()->showMessage(
                tr("Download failed: %1").arg(result.error()), 5000);
            return;
        }
        showViewer(message.mediaId, result.value());
    });
}

MediaViewer *MainWindow::ensureViewer()
{
    if (!m_viewer) {
        m_viewer = new MediaViewer(this);
        connect(m_viewer, &MediaViewer::saveRequested, this, &MainWindow::saveMediaAs);
        connect(m_viewer, &MediaViewer::deleteRequested, this, [this](const QString &id) {
            auto *model = m_chatView->messageModel();
            for (int row = 0; row < model->count(); ++row) {
                const Message message = model->messageAt(row);
                if ((message.mediaId == id
                     || splitMediaIds(message.mediaId).contains(id))
                    && m_messageService) {
                    m_messageService->deleteLocal(message.clientMsgId, [](Result<bool>) {});
                    break;
                }
            }
        });
        connect(m_viewer, &MediaViewer::showInChatRequested, this,
                [this](const QString &id) {
                    auto *model = m_chatView->messageModel();
                    for (int row = 0; row < model->count(); ++row) {
                        const Message candidate = model->messageAt(row);
                        if (candidate.mediaId == id
                            || splitMediaIds(candidate.mediaId).contains(id)) {
                            const QModelIndex index = model->index(row, 0);
                            m_chatView->messageView()->scrollTo(index);
                            m_chatView->messageView()->setCurrentIndex(index);
                            break;
                        }
                    }
                });
        connect(m_viewer, &MediaViewer::openExternalRequested, this,
                [this](const QString &id) {
                    const QString local = m_media ? m_media->cachedFile(id) : QString();
                    if (!local.isEmpty()) {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(local));
                    }
                });
    }
    return m_viewer;
}

void MainWindow::showViewer(const QString &mediaId, const QString &path)
{
    QVector<MediaViewerItem> items;
    int index = 0;
    auto *model = m_chatView->messageModel();
    for (int row = 0; row < model->count(); ++row) {
        const Message message = model->messageAt(row);
        if (message.kind != MediaKind::Photo && message.kind != MediaKind::Video) {
            continue;
        }
        MediaViewerItem item;
        item.mediaId = message.mediaId;
        item.title = message.mediaName;
        if (message.kind == MediaKind::Video) {
            item.subtitle = message.mediaSize > 0
                ? tr("%1 MB").arg(QString::number(
                      message.mediaSize / (1024.0 * 1024.0), 'f', 1))
                : QString();
        }
        item.kind = message.kind;
        item.path = message.mediaId == mediaId ? path : m_media->cachedFile(message.mediaId);
        if (!item.path.isEmpty()) {
            item.image.load(item.path);
        }
        if (item.image.isNull() && m_media) {
            item.image = m_media->preview(message.mediaId, 1280);
        }
        if (message.mediaId == mediaId) {
            index = items.size();
        }
        items.append(item);
    }
    MediaViewer *viewer = ensureViewer();
    viewer->setItems(items, index);
    if (!viewer->isVisible()) {
        viewer->showFullScreen();
    } else {
        viewer->raise();
        viewer->activateWindow();
    }
    viewer->showCurrent();
}

void MainWindow::showAlbumViewer(const QString &clientMsgId)
{
    if (!m_media) {
        return;
    }
    auto *model = m_chatView->messageModel();
    const int row = model->indexOf(clientMsgId);
    if (row < 0) {
        return;
    }
    const Message message = model->messageAt(row);
    const QStringList ids = splitMediaIds(message.mediaId);
    if (ids.isEmpty()) {
        return;
    }
    statusBar()->showMessage(tr("Downloading album…"));
    struct State
    {
        QVector<MediaViewerItem> items;
        int index = 0;
        int failed = 0;
    };
    auto state = std::make_shared<State>();
    auto next = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakNext = next;
    *next = [this, ids, message, state, weakNext] {
        if (state->index >= ids.size()) {
            if (state->items.isEmpty()) {
                statusBar()->showMessage(tr("Album download failed."), 5000);
                return;
            }
            MediaViewer *viewer = ensureViewer();
            viewer->setItems(state->items, 0);
            if (!viewer->isVisible()) {
                viewer->showFullScreen();
            } else {
                viewer->raise();
                viewer->activateWindow();
            }
            viewer->showCurrent();
            return;
        }
        const QString mediaId = ids.at(state->index);
        m_media->download(mediaId, [this, mediaId, message, state, weakNext](
                                       Result<QString> downloaded) {
            if (downloaded.isOk()) {
                MediaViewerItem item;
                item.mediaId = mediaId;
                item.title = message.mediaName;
                item.subtitle = tr("Album · %1").arg(message.mediaSize > 0
                    ? tr("%1 MB").arg(QString::number(
                          message.mediaSize / (1024.0 * 1024.0), 'f', 1))
                    : QString());
                item.kind = MediaKind::Photo;
                item.path = downloaded.value();
                item.image.load(item.path);
                if (item.image.isNull() && m_media) {
                    item.image = m_media->preview(mediaId, 1280);
                }
                state->items.append(item);
            } else {
                ++state->failed;
            }
            ++state->index;
            if (auto nextFn = weakNext.lock()) {
                (*nextFn)();
            }
        });
    };
    (*next)();
}

void MainWindow::saveMediaAs(const QString &mediaId)
{
    if (!m_media) {
        return;
    }
    const QString local = m_media->cachedFile(mediaId);
    if (local.isEmpty()) {
        m_media->download(mediaId, [this, mediaId](Result<QString> result) {
            if (result.isOk()) {
                saveMediaAs(mediaId);
            }
        });
        return;
    }
    QString name;
    auto *model = m_chatView->messageModel();
    for (int row = 0; row < model->count(); ++row) {
        const Message message = model->messageAt(row);
        if (message.mediaId == mediaId) {
            name = QFileInfo(message.mediaName).fileName();
            break;
        }
    }
    if (name.isEmpty()) {
        name = mediaId;
    }
    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString target = QFileDialog::getSaveFileName(
        this, tr("Save attachment"), QDir(folder).filePath(name));
    if (target.isEmpty()) {
        return;
    }
    QFile::remove(target);
    if (!QFile::copy(local, target)) {
        statusBar()->showMessage(tr("Could not save the file."), 4000);
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

void MainWindow::jumpToMessage(const QString &chatId, const QString &clientMsgId)
{
    if (clientMsgId.isEmpty()) {
        openChatById(chatId);
        return;
    }
    if (m_currentChat.chatId == chatId && m_chatView->messageModel()->indexOf(clientMsgId) >= 0) {
        m_chatView->jumpToMessage(clientMsgId);
        return;
    }
    m_pendingJumpId = clientMsgId;
    openChatById(chatId);
}

void MainWindow::openForwardDialog(const QStringList &sourceIds)
{
    if (sourceIds.isEmpty() || !m_messageService) {
        return;
    }
    auto showDialog = [this, sourceIds](QVector<Chat> chats) {
        ForwardDialog dialog(this);
        dialog.setChats(chats);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QStringList targets = dialog.selectedChatIds();
        if (targets.isEmpty()) {
            return;
        }
        m_messageService->forwardMessages(
            sourceIds, targets, [this](Result<int> result) {
                statusBar()->showMessage(
                    result.isOk() ? tr("Forwarded: %1").arg(result.value())
                                  : tr("Could not forward: %1").arg(result.error()),
                    4000);
            });
    };
    QVector<Chat> chats = m_sidebar->chatList()->chats();
    if (!chats.isEmpty() || !m_chatService) {
        showDialog(chats);
        return;
    }
    m_chatService->listChats([showDialog](Result<QVector<Chat>> result) {
        showDialog(result.isOk() ? result.value() : QVector<Chat>{});
    });
}

void MainWindow::refreshPins()
{
    if (!m_messageService || m_currentChat.chatId.isEmpty()) {
        m_chatView->setPinnedMessages({});
        return;
    }
    const QString chatId = m_currentChat.chatId;
    m_messageService->listPinned(chatId, [this, chatId](Result<QVector<Message>> result) {
        if (m_currentChat.chatId != chatId) {
            return;
        }
        m_chatView->setPinnedMessages(result.isOk() ? result.value() : QVector<Message>{});
    });
}

} // namespace mini
