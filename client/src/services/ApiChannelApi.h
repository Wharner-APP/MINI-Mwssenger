#pragma once

#include "services/ChannelApi.h"

namespace mini::net {
class ApiClient;
}

namespace mini::services {

// Adapts ApiClient's typed channel calls to ChannelApi.
class ApiChannelApi : public ChannelApi
{
public:
    explicit ApiChannelApi(net::ApiClient *api);

    void listChannels(ChannelsDone done) override;
    void createChannel(const net::ChannelCreateRequest &request, ChannelDone done) override;
    void getChannel(const QString &channelId, ChannelDone done) override;
    void updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                       ChannelDone done) override;
    void deleteChannel(const QString &channelId, EmptyDone done) override;
    void subscribe(const QString &channelId, const QString &token,
                   StatusDone done) override;
    void unsubscribe(const QString &channelId, StatusDone done) override;
    void members(const QString &channelId, MembersDone done) override;
    void setRole(const QString &channelId, const QString &userId, const QString &role,
                 quint32 permissions, MemberDone done) override;
    void createInvite(const QString &channelId, InviteDone done) override;
    void recordView(const QString &channelId, const QString &postId,
                    ViewDone done) override;

private:
    net::ApiClient *m_api = nullptr;
};

} // namespace mini::services
