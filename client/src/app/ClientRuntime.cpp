#include "app/ClientRuntime.h"

#include "core/Log.h"
#include "crypto/Kdf.h"
#include "crypto/Sodium.h"
#include "net/ApiClient.h"
#include "net/TlsPin.h"
#include "net/WsClient.h"
#include "services/ChatService.h"
#include "services/ContactService.h"
#include "services/MessageService.h"
#include "services/SearchService.h"
#include "services/SessionCrypto.h"
#include "storage/StorageWorker.h"

#include <QDir>
#include <QJsonObject>

namespace mini {

ClientRuntime::ClientRuntime(QObject *parent) : QObject(parent) {}

ClientRuntime::~ClientRuntime()
{
    stop();
}

Result<bool> ClientRuntime::start(const Config &config)
{
    if (m_running) {
        stop();
    }
    if (!config.endpoint.isValid() || config.dataDir.isEmpty()) {
        return Result<bool>::error(QStringLiteral("runtime_config_invalid"));
    }
    if (!crypto::Sodium::init()) {
        return Result<bool>::error(QStringLiteral("crypto_unavailable"));
    }

    QDir().mkpath(config.dataDir);
    const auto dbKey = crypto::Kdf::deriveDbKey(config.kenc);
    const auto keyWrap = crypto::Kdf::deriveKeyWrap(config.kenc);
    if (dbKey.isError() || keyWrap.isError()) {
        return Result<bool>::error(QStringLiteral("key_derivation_failed"));
    }

    m_storage = new storage::StorageWorker(this);
    const QString databasePath = QDir(config.dataDir).filePath(QStringLiteral("mini.db"));
    const QString keysPath = QDir(config.dataDir).filePath(QStringLiteral("keys.bin"));
    const QString backupsDir = QDir(config.dataDir).filePath(QStringLiteral("backups"));
    auto opened = m_storage->open(databasePath, dbKey.value(), backupsDir, keysPath);
    if (opened.isError()) {
        stop();
        return Result<bool>::error(opened.error());
    }

    const auto identity =
        crypto::IdentityKeys::loadOrCreate(keysPath, keyWrap.value(), config.deviceId);
    if (identity.isError()) {
        stop();
        return Result<bool>::error(identity.error());
    }
    m_identity = identity.value();

    m_config = config;
    m_api = new net::ApiClient(config.endpoint, config.pin, this);
    m_api->setAccessToken(config.accessToken);
    m_api->setRefreshToken(config.refreshToken);
    m_api->setDeviceId(config.deviceId);
    connect(m_api, &net::ApiClient::authenticationRequired, this,
            &ClientRuntime::authenticationRequired);

    m_ws = new net::WsClient(m_api, config.pin, this);
    m_transport = new services::ApiMessageTransport(m_api, m_ws, this);
    m_transport->setDeviceId(config.deviceId);

    m_crypto = new services::SessionCrypto(
        m_identity, [this](const QString &login,
                           std::function<void(Result<net::PeerKeys>)> done) {
            m_api->fetchPeerKeys(login, [done = std::move(done)](net::ApiResult<net::PeerKeys> result) {
                if (result.isError()) {
                    done(Result<net::PeerKeys>::error(result.error().code.isEmpty()
                                                          ? QStringLiteral("peer_keys_unavailable")
                                                          : result.error().code));
                    return;
                }
                done(Result<net::PeerKeys>::ok(result.value()));
            });
        });

    m_messages = new services::MessageService(m_storage, this);
    m_messages->setSelfLogin(config.selfLogin);
    m_messages->setCryptoSession(m_crypto);
    m_messages->setTransport(m_transport);

    m_chat = new services::ChatService(m_storage, this);
    m_chat->setSelfLogin(config.selfLogin);

    m_contacts = new services::ContactService(m_storage, this);
    m_contacts->setLookup([this](const QString &login,
                                 std::function<void(Result<services::PeerProfile>)> done) {
        m_api->lookupUser(login, [login, done = std::move(done)](
                                     net::ApiResult<net::AccountProfile> result) {
            if (result.isError()) {
                done(Result<services::PeerProfile>::error(
                    result.error().code.isEmpty() ? QStringLiteral("directory_unavailable")
                                                  : result.error().code));
                return;
            }
            services::PeerProfile profile;
            profile.login = result.value().login.isEmpty() ? login : result.value().login;
            profile.displayName = result.value().displayName;
            profile.userId = result.value().userId;
            profile.online = result.value().online;
            profile.lastActiveAt = result.value().lastActiveAt;
            profile.found = !profile.userId.isEmpty() || !profile.login.isEmpty();
            done(Result<services::PeerProfile>::ok(profile));
        });
    });
    m_contacts->setSelfSource(
        [this](std::function<void(Result<services::SelfProfile>)> done) {
            m_api->lookupUser(m_config.selfLogin,
                              [done = std::move(done)](net::ApiResult<net::AccountProfile> result) {
                if (result.isError()) {
                    done(Result<services::SelfProfile>::error(QStringLiteral("profile_unavailable")));
                    return;
                }
                services::SelfProfile self;
                self.userId = result.value().userId;
                self.login = result.value().login;
                self.displayName = result.value().displayName;
                self.bio = result.value().bio;
                self.online = result.value().online;
                self.lastActiveAt = result.value().lastActiveAt;
                done(Result<services::SelfProfile>::ok(self));
            });
        },
        [this](const QString &displayName, const QString &bio,
               std::function<void(Result<services::SelfProfile>)> done) {
            net::ProfilePatch patch;
            patch.displayName = displayName;
            patch.bio = bio;
            m_api->patchMe(patch, [done = std::move(done)](
                                      net::ApiResult<net::AccountProfile> result) {
                if (result.isError()) {
                    done(Result<services::SelfProfile>::error(
                        result.error().code.isEmpty() ? QStringLiteral("profile_update_failed")
                                                      : result.error().code));
                    return;
                }
                services::SelfProfile self;
                self.userId = result.value().userId;
                self.login = result.value().login;
                self.displayName = result.value().displayName;
                self.bio = result.value().bio;
                self.online = result.value().online;
                self.lastActiveAt = result.value().lastActiveAt;
                done(Result<services::SelfProfile>::ok(self));
            });
        });

    m_search = new services::SearchService(m_storage, this);

    m_running = true;
    m_keysPublished = false;
    publishKeys();
    m_ws->start(config.endpoint, config.accessToken);
    Log::info(QStringLiteral("runtime.started"),
              {{QStringLiteral("login"), config.selfLogin}});
    return Result<bool>::ok(true);
}

void ClientRuntime::publishKeys()
{
    if (!m_api || !m_identity.isValid()) {
        emit keysPublishFailed(QStringLiteral("identity_invalid"));
        return;
    }
    net::PeerKeys keys;
    keys.login = m_config.selfLogin;
    keys.deviceId = m_config.deviceId;
    keys.identityKey = m_identity.identity.publicKey;
    keys.signedPrekey = m_identity.signedPrekey.pair.publicKey;
    keys.signedPrekeySignature = m_identity.signedPrekey.signature;
    keys.signedPrekeyId = m_identity.signedPrekey.id;
    for (const crypto::OneTimePrekey &prekey : m_identity.oneTimePrekeys) {
        keys.oneTimePrekeys.append(qMakePair(prekey.id, prekey.pair.publicKey));
    }

    m_api->putPrekeys(keys, [this](net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            emit keysPublishFailed(result.error().code.isEmpty()
                                       ? QStringLiteral("keys_publish_failed")
                                       : result.error().code);
            return;
        }
        m_keysPublished = true;
        emit keysPublished();
    });
}

void ClientRuntime::stop()
{
    if (m_ws) {
        m_ws->stop();
    }
    if (m_storage) {
        m_storage->close();
    }
    delete m_transport;
    m_transport = nullptr;
    for (QObject *child : {static_cast<QObject *>(m_api), static_cast<QObject *>(m_ws),
                           static_cast<QObject *>(m_messages), static_cast<QObject *>(m_chat),
                           static_cast<QObject *>(m_contacts), static_cast<QObject *>(m_search)}) {
        delete child;
    }
    delete m_crypto;
    delete m_storage;
    m_api = nullptr;
    m_ws = nullptr;
    m_messages = nullptr;
    m_chat = nullptr;
    m_contacts = nullptr;
    m_search = nullptr;
    m_crypto = nullptr;
    m_storage = nullptr;
    m_running = false;}

} // namespace mini
