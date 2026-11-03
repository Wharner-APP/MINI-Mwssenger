#pragma once

#include "services/PrivacyApi.h"

namespace mini::net {
class ApiClient;
}

namespace mini::services {

// Adapts ApiClient's privacy, block and device calls to PrivacyApi.
class ApiPrivacyApi : public PrivacyApi
{
public:
    explicit ApiPrivacyApi(net::ApiClient *api);

    void getSettings(SettingsDone done) override;
    void updateSettings(const net::PrivacySettings &settings, SettingsDone done) override;
    void listBlocks(BlocksDone done) override;
    void blockUser(const QString &userOrLogin, EmptyDone done) override;
    void unblockUser(const QString &userId, EmptyDone done) override;
    void listDevices(DevicesDone done) override;
    void revokeDevice(const QString &deviceId, EmptyDone done) override;
    void revokeAllDevices(RevokeDone done) override;

private:
    net::ApiClient *m_api = nullptr;
};

} // namespace mini::services
