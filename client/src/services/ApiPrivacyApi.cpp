#include "services/ApiPrivacyApi.h"

#include "net/ApiClient.h"

namespace mini::services {

namespace {

template <typename T>
net::ApiError unavailable()
{
    return net::ApiError::protocol(QStringLiteral("api_unavailable"));
}

} // namespace

ApiPrivacyApi::ApiPrivacyApi(net::ApiClient *api) : m_api(api) {}

void ApiPrivacyApi::getSettings(SettingsDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::PrivacySettings>::fail(unavailable<net::PrivacySettings>()));
        return;
    }
    m_api->getPrivacySettings(std::move(done));
}

void ApiPrivacyApi::updateSettings(const net::PrivacySettings &settings, SettingsDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::PrivacySettings>::fail(unavailable<net::PrivacySettings>()));
        return;
    }
    m_api->updatePrivacySettings(settings, std::move(done));
}

void ApiPrivacyApi::listBlocks(BlocksDone done)
{
    if (!m_api) {
        done(net::ApiResult<QList<net::BlockedUserInfo>>::fail(
            unavailable<QList<net::BlockedUserInfo>>()));
        return;
    }
    m_api->listBlocks(std::move(done));
}

void ApiPrivacyApi::blockUser(const QString &userOrLogin, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable<net::Empty>()));
        return;
    }
    m_api->blockUser(userOrLogin, std::move(done));
}

void ApiPrivacyApi::unblockUser(const QString &userId, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable<net::Empty>()));
        return;
    }
    m_api->unblockUser(userId, std::move(done));
}

void ApiPrivacyApi::listDevices(DevicesDone done)
{
    if (!m_api) {
        done(net::ApiResult<QList<net::DeviceRecord>>::fail(
            unavailable<QList<net::DeviceRecord>>()));
        return;
    }
    m_api->listDevices(std::move(done));
}

void ApiPrivacyApi::revokeDevice(const QString &deviceId, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable<net::Empty>()));
        return;
    }
    m_api->revokeDevice(deviceId, std::move(done));
}

void ApiPrivacyApi::revokeAllDevices(RevokeDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::RevokeAllResult>::fail(
            unavailable<net::RevokeAllResult>()));
        return;
    }
    m_api->revokeAllDevices(std::move(done));
}

} // namespace mini::services
