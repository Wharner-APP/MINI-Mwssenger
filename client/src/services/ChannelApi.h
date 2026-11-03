#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QList>
#include <QString>

#include <functional>

namespace mini::services {

// Network seam of ChannelService. Production uses ApiChannelApi; tests
// implement it in memory without a server.
class ChannelApi
{
public:
    virtual ~ChannelApi() = default;

    using ChannelsDone = std::function<void(net::ApiResult<QList<net::ChannelInfo>>)>;
    using ChannelDone = std::function<void(net::ApiResult<net::ChannelInfo>)>;
    using MemberDone = std::function<void(net::ApiResult<net::ChannelMemberInfo>)>;
    using MembersDone = std::function<void(net::ApiResult<QList<net::ChannelMemberInfo>>)>;
    using EmptyDone = std::function<void(net::ApiResult<net::Empty>)>;
    using StatusDone = std::function<void(net::ApiResult<QString>)>;
    using InviteDone = std::function<void(net::ApiResult<net::ChannelInvite>)>;
    using ViewDone = std::function<void(net::ApiResult<net::ChannelViewInfo>)>;

    virtual void listChannels(ChannelsDone done) = 0;
    virtual void createChannel(const net::ChannelCreateRequest &request,
                               ChannelDone done) = 0;
    virtual void getChannel(const QString &channelId, ChannelDone done) = 0;
    virtual void updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                               ChannelDone done) = 0;
    virtual void deleteChannel(const QString &channelId, EmptyDone done) = 0;
    virtual void subscribe(const QString &channelId, const QString &token,
                           StatusDone done) = 0;
    virtual void unsubscribe(const QString &channelId, StatusDone done) = 0;
    virtual void members(const QString &channelId, MembersDone done) = 0;
    virtual void setRole(const QString &channelId, const QString &userId,
                         const QString &role, quint32 permissions, MemberDone done) = 0;
    virtual void createInvite(const QString &channelId, InviteDone done) = 0;
    virtual void recordView(const QString &channelId, const QString &postId,
                            ViewDone done) = 0;
};

} // namespace mini::services
