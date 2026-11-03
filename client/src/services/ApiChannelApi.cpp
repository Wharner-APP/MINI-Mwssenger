#include "services/ApiChannelApi.h"

#include "net/ApiClient.h"

namespace mini::services {

ApiChannelApi::ApiChannelApi(net::ApiClient *api) : m_api(api)
{
}

void ApiChannelApi::listChannels(ChannelsDone done)
{
    m_api->listChannels(std::move(done));
}

void ApiChannelApi::createChannel(const net::ChannelCreateRequest &request, ChannelDone done)
{
    m_api->createChannel(request, std::move(done));
}

void ApiChannelApi::getChannel(const QString &channelId, ChannelDone done)
{
    m_api->getChannel(channelId, std::move(done));
}

void ApiChannelApi::updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                                  ChannelDone done)
{
    m_api->updateChannel(channelId, patch, std::move(done));
}

void ApiChannelApi::deleteChannel(const QString &channelId, EmptyDone done)
{
    m_api->deleteChannel(channelId, std::move(done));
}

void ApiChannelApi::subscribe(const QString &channelId, const QString &token,
                              StatusDone done)
{
    m_api->subscribeChannel(channelId, token, std::move(done));
}

void ApiChannelApi::unsubscribe(const QString &channelId, StatusDone done)
{
    m_api->unsubscribeChannel(channelId, std::move(done));
}

void ApiChannelApi::members(const QString &channelId, MembersDone done)
{
    m_api->listChannelMembers(channelId, std::move(done));
}

void ApiChannelApi::setRole(const QString &channelId, const QString &userId,
                            const QString &role, quint32 permissions, MemberDone done)
{
    m_api->setChannelRole(channelId, userId, role, permissions, std::move(done));
}

void ApiChannelApi::createInvite(const QString &channelId, InviteDone done)
{
    m_api->createChannelInvite(channelId, std::move(done));
}

void ApiChannelApi::recordView(const QString &channelId, const QString &postId,
                               ViewDone done)
{
    m_api->recordChannelView(channelId, postId, std::move(done));
}

} // namespace mini::services
