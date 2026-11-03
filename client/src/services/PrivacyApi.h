#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QList>
#include <QString>

#include <functional>

namespace mini::services {

// Network seam of PrivacyService (§11.12 #177-182, §28.16). Production uses
// ApiPrivacyApi; tests implement it in memory without a server.
class PrivacyApi
{
public:
    virtual ~PrivacyApi() = default;

    using SettingsDone = std::function<void(net::ApiResult<net::PrivacySettings>)>;
    using BlocksDone = std::function<void(net::ApiResult<QList<net::BlockedUserInfo>>)>;
    using DevicesDone = std::function<void(net::ApiResult<QList<net::DeviceRecord>>)>;
    using EmptyDone = std::function<void(net::ApiResult<net::Empty>)>;
    using RevokeDone = std::function<void(net::ApiResult<net::RevokeAllResult>)>;

    virtual void getSettings(SettingsDone done) = 0;
    virtual void updateSettings(const net::PrivacySettings &settings,
                                SettingsDone done) = 0;
    virtual void listBlocks(BlocksDone done) = 0;
    virtual void blockUser(const QString &userOrLogin, EmptyDone done) = 0;
    virtual void unblockUser(const QString &userId, EmptyDone done) = 0;
    virtual void listDevices(DevicesDone done) = 0;
    virtual void revokeDevice(const QString &deviceId, EmptyDone done) = 0;
    virtual void revokeAllDevices(RevokeDone done) = 0;
};

} // namespace mini::services
