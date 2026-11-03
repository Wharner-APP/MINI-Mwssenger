#pragma once

#include "services/GroupApi.h"

namespace mini::net {
class ApiClient;
}

namespace mini::services {

// Adapts ApiClient's typed group calls to GroupApi. No JSON escapes this layer.
class ApiGroupApi : public GroupApi
{
public:
    explicit ApiGroupApi(net::ApiClient *api);

    void listGroups(GroupsDone done) override;
    void createGroup(const net::GroupCreateRequest &request, GroupDone done) override;
    void getGroup(const QString &groupId, GroupDone done) override;
    void updateGroup(const QString &groupId, const net::GroupPatch &patch,
                     GroupDone done) override;
    void addMember(const QString &groupId, const QString &userIdOrLogin,
                   MemberDone done) override;
    void removeMember(const QString &groupId, const QString &userId, bool ban,
                      int durationHours, EmptyDone done) override;
    void setRole(const QString &groupId, const QString &userId, const QString &role,
                 quint32 permissions, MemberDone done) override;
    void transferOwnership(const QString &groupId, const QString &userId,
                           GroupDone done) override;
    void createInvite(const QString &groupId, InviteDone done) override;
    void join(const QString &token, const QString &message, JoinDone done) override;
    void requestJoin(const QString &groupId, const QString &message,
                     RequestDone done) override;
    void listRequests(const QString &groupId, const QString &status,
                      RequestsDone done) override;
    void approve(const QString &groupId, const QString &userId, bool approve,
                 RequestDone done) override;
    void ban(const QString &groupId, const QString &userId, bool permanent,
             int durationHours, const QString &reason, EmptyDone done) override;
    void unban(const QString &groupId, const QString &userId, EmptyDone done) override;

private:
    net::ApiClient *m_api = nullptr;
};

} // namespace mini::services
