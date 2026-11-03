#pragma once

#include "core/Result.h"
#include "crypto/IdentityKeys.h"
#include "net/Endpoint.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

namespace mini::net {
class ApiClient;
class TlsPin;
class WsClient;
} // namespace mini::net

namespace mini::storage {
class StorageWorker;
} // namespace mini::storage

namespace mini::services {
class BotService;
class ChannelService;
class ChatService;
class ContactService;
class ExportService;
class FolderService;
class GroupService;
class ImportService;
class MediaService;
class MessageService;
class PollService;
class PrivacyService;
class SearchService;
class SessionCrypto;
class StickerService;
class ApiMediaTransport;
class ApiMessageTransport;
class ApiChannelApi;
class ApiGroupApi;
class ApiBotApi;
class ApiPrivacyApi;
} // namespace mini::services

namespace mini {

// Everything a signed-in session needs: storage, transport and the four
// services the window talks to. Application and the E2E harness both build it,
// so the real window and the test run the same wiring.
class ClientRuntime : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        net::Endpoint endpoint;
        net::TlsPin *pin = nullptr;
        QString dataDir;
        QString accessToken;
        QString refreshToken;
        QString deviceId;
        QString selfLogin;
        QString selfUserId;
        QByteArray kenc; // K_enc; only DB_KEY and KEY_WRAP are derived from it
        // §49: K_mac from the sign.in key branch seeds the export key. An
        // empty value disables export/import with backup.key_missing.
        QByteArray kmac;
    };

    explicit ClientRuntime(QObject *parent = nullptr);
    ~ClientRuntime() override;

    Result<bool> start(const Config &config);
    void stop();

    [[nodiscard]] services::ChatService *chatService() const { return m_chat; }
    [[nodiscard]] services::ContactService *contactService() const { return m_contacts; }
    [[nodiscard]] services::GroupService *groupService() const { return m_groups; }
    [[nodiscard]] services::ChannelService *channelService() const { return m_channels; }
    [[nodiscard]] services::MessageService *messageService() const { return m_messages; }
    [[nodiscard]] services::PollService *pollService() const { return m_polls; }
    [[nodiscard]] services::FolderService *folderService() const { return m_folders; }
    [[nodiscard]] services::MediaService *mediaService() const { return m_media; }
    [[nodiscard]] services::StickerService *stickerService() const { return m_stickers; }
    [[nodiscard]] services::SearchService *searchService() const { return m_search; }
    [[nodiscard]] services::BotService *botService() const { return m_bots; }
    [[nodiscard]] services::PrivacyService *privacyService() const { return m_privacy; }
    // §49: encrypted .minibackup export and merge import.
    [[nodiscard]] services::ExportService *exportService() const { return m_export; }
    [[nodiscard]] services::ImportService *importService() const { return m_import; }
    [[nodiscard]] storage::StorageWorker *storage() const { return m_storage; }
    [[nodiscard]] net::ApiClient *api() const { return m_api; }
    [[nodiscard]] net::WsClient *ws() const { return m_ws; }
    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] bool hasPublishedKeys() const { return m_keysPublished; }

signals:
    // The public prekey bundle is on the server; peers may now open a chat.
    void keysPublished();
    void keysPublishFailed(const QString &code);
    void authenticationRequired();

private:
    void publishKeys();

    storage::StorageWorker *m_storage = nullptr;
    net::ApiClient *m_api = nullptr;
    net::WsClient *m_ws = nullptr;
    services::ChatService *m_chat = nullptr;
    services::ContactService *m_contacts = nullptr;
    services::GroupService *m_groups = nullptr;
    services::ChannelService *m_channels = nullptr;
    services::MessageService *m_messages = nullptr;
    services::PollService *m_polls = nullptr;
    services::FolderService *m_folders = nullptr;
    services::MediaService *m_media = nullptr;
    services::StickerService *m_stickers = nullptr;
    services::SearchService *m_search = nullptr;
    services::BotService *m_bots = nullptr;
    services::PrivacyService *m_privacy = nullptr;
    services::ExportService *m_export = nullptr;
    services::ImportService *m_import = nullptr;
    services::SessionCrypto *m_crypto = nullptr;
    services::ApiMessageTransport *m_transport = nullptr;
    services::ApiMediaTransport *m_mediaTransport = nullptr;
    services::ApiGroupApi *m_groupApi = nullptr;
    services::ApiChannelApi *m_channelApi = nullptr;
    services::ApiBotApi *m_botApi = nullptr;
    services::ApiPrivacyApi *m_privacyApi = nullptr;
    crypto::IdentityState m_identity;
    Config m_config;
    bool m_running = false;
    bool m_keysPublished = false;
};

} // namespace mini
