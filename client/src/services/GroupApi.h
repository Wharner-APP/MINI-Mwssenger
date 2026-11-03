#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QList>
#include <QString>

#include <functional>

namespace mini::services {

// Network seam of GroupService. Production uses ApiGroupApi; tests implement
// it in memory without a server.
class GroupApi
{
public:
    virtual ~GroupApi() = default;

    using GroupsDone = std::function<void(net::ApiResult<QList<net::GroupInfo>>)>;
    using GroupDone = std::function<void(net::ApiResult<net::GroupInfo>)>;
    using MemberDone = std::function<void(net::ApiResult<net::GroupMemberInfo>)>;
    using EmptyDone = std::function<void(net::ApiResult<net::Empty>)>;
    using InviteDone = std::function<void(net::ApiResult<net::GroupInvite>)>;
    using JoinDone = std::function<void(net::ApiResult<net::GroupJoinResult>)>;
    using RequestDone = std::function<void(net::ApiResult<net::GroupJoinRequestInfo>)>;
    using RequestsDone =
        std::function<void(net::ApiResult<QList<net::GroupJoinRequestInfo>>)>;

    virtual void listGroups(GroupsDone done) = 0;
    virtual void createGroup(const net::GroupCreateRequest &request, GroupDone done) = 0;
    virtual void getGroup(const QString &groupId, GroupDone done) = 0;
    virtual void updateGroup(const QString &groupId, const net::GroupPatch &patch,
                             GroupDone done) = 0;
    virtual void addMember(const QString &groupId, const QString &userIdOrLogin,
                           MemberDone done) = 0;
    virtual void removeMember(const QString &groupId, const QString &userId, bool ban,
                              int durationHours, EmptyDone done) = 0;
    virtual void setRole(const QString &groupId, const QString &userId,
                         const QString &role, quint32 permissions, MemberDone done) = 0;
    virtual void transferOwnership(const QString &groupId, const QString &userId,
                                   GroupDone done) = 0;
    virtual void createInvite(const QString &groupId, InviteDone done) = 0;
    virtual void join(const QString &token, const QString &message, JoinDone done) = 0;
    virtual void requestJoin(const QString &groupId, const QString &message,
                             RequestDone done) = 0;
    virtual void listRequests(const QString &groupId, const QString &status,
                              RequestsDone done) = 0;
    virtual void approve(const QString &groupId, const QString &userId, bool approve,
                         RequestDone done) = 0;
    virtual void ban(const QString &groupId, const QString &userId, bool permanent,
                     int durationHours, const QString &reason, EmptyDone done) = 0;
    virtual void unban(const QString &groupId, const QString &userId,
                       EmptyDone done) = 0;
};

} // namespace mini::services
