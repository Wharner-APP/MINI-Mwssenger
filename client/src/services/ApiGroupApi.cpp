#include "services/ApiGroupApi.h"

#include "net/ApiClient.h"

namespace mini::services {
namespace {

net::ApiError unavailable()
{
    return net::ApiError::protocol(QStringLiteral("api_unavailable"));
}

} // namespace

ApiGroupApi::ApiGroupApi(net::ApiClient *api) : m_api(api)
{
}

void ApiGroupApi::listGroups(GroupsDone done)
{
    if (!m_api) {
        done(net::ApiResult<QList<net::GroupInfo>>::fail(unavailable()));
        return;
    }
    m_api->listGroups(std::move(done));
}

void ApiGroupApi::createGroup(const net::GroupCreateRequest &request, GroupDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupInfo>::fail(unavailable()));
        return;
    }
    m_api->createGroup(request, std::move(done));
}

void ApiGroupApi::getGroup(const QString &groupId, GroupDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupInfo>::fail(unavailable()));
        return;
    }
    m_api->getGroup(groupId, std::move(done));
}

void ApiGroupApi::updateGroup(const QString &groupId, const net::GroupPatch &patch,
                              GroupDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupInfo>::fail(unavailable()));
        return;
    }
    m_api->updateGroup(groupId, patch, std::move(done));
}

void ApiGroupApi::addMember(const QString &groupId, const QString &userIdOrLogin,
                            MemberDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupMemberInfo>::fail(unavailable()));
        return;
    }
    m_api->addGroupMember(groupId, userIdOrLogin, std::move(done));
}

void ApiGroupApi::removeMember(const QString &groupId, const QString &userId, bool ban,
                               int durationHours, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable()));
        return;
    }
    m_api->removeGroupMember(groupId, userId, ban, durationHours, std::move(done));
}

void ApiGroupApi::setRole(const QString &groupId, const QString &userId,
                          const QString &role, quint32 permissions, MemberDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupMemberInfo>::fail(unavailable()));
        return;
    }
    m_api->setGroupRole(groupId, userId, role, permissions, std::move(done));
}

void ApiGroupApi::transferOwnership(const QString &groupId, const QString &userId,
                                    GroupDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupInfo>::fail(unavailable()));
        return;
    }
    m_api->transferGroupOwnership(groupId, userId, std::move(done));
}

void ApiGroupApi::createInvite(const QString &groupId, InviteDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupInvite>::fail(unavailable()));
        return;
    }
    m_api->createGroupInvite(groupId, std::move(done));
}

void ApiGroupApi::join(const QString &token, const QString &message, JoinDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupJoinResult>::fail(unavailable()));
        return;
    }
    m_api->joinGroup(token, message, std::move(done));
}

void ApiGroupApi::requestJoin(const QString &groupId, const QString &message,
                              RequestDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupJoinRequestInfo>::fail(unavailable()));
        return;
    }
    m_api->requestGroupJoin(groupId, message, std::move(done));
}

void ApiGroupApi::listRequests(const QString &groupId, const QString &status,
                               RequestsDone done)
{
    if (!m_api) {
        done(net::ApiResult<QList<net::GroupJoinRequestInfo>>::fail(unavailable()));
        return;
    }
    m_api->listGroupRequests(groupId, status, std::move(done));
}

void ApiGroupApi::approve(const QString &groupId, const QString &userId, bool approve,
                          RequestDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::GroupJoinRequestInfo>::fail(unavailable()));
        return;
    }
    m_api->approveGroupRequest(groupId, userId, approve, std::move(done));
}

void ApiGroupApi::ban(const QString &groupId, const QString &userId, bool permanent,
                      int durationHours, const QString &reason, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable()));
        return;
    }
    m_api->banGroupMember(groupId, userId, permanent, durationHours, reason, std::move(done));
}

void ApiGroupApi::unban(const QString &groupId, const QString &userId, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(unavailable()));
        return;
    }
    m_api->unbanGroupMember(groupId, userId, std::move(done));
}

} // namespace mini::services
