#include "services/ApiBotApi.h"

#include "net/ApiClient.h"

namespace mini::services {

ApiBotApi::ApiBotApi(net::ApiClient *api) : m_api(api) {}

void ApiBotApi::listBots(BotsDone done)
{
    if (!m_api) {
        done(net::ApiResult<QList<net::BotInfo>>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->listBots(std::move(done));
}

void ApiBotApi::createBot(const net::BotCreateRequest &request, CreateDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::BotCreateResult>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->createBot(request, std::move(done));
}

void ApiBotApi::getBot(const QString &botId, BotDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::BotInfo>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->getBot(botId, std::move(done));
}

void ApiBotApi::rotateToken(const QString &botId, TokenDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::BotTokenResult>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->rotateBotToken(botId, std::move(done));
}

void ApiBotApi::deleteBot(const QString &botId, EmptyDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::Empty>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->deleteBot(botId, std::move(done));
}

void ApiBotApi::sendMessage(const QString &botId, const QString &text, SendDone done)
{
    if (!m_api) {
        done(net::ApiResult<net::BotSendResult>::fail(
            net::ApiError::protocol(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->sendBotMessage(botId, text, std::move(done));
}

} // namespace mini::services
