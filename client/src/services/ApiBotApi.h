#pragma once

#include "services/BotApi.h"

namespace mini::net {
class ApiClient;
}

namespace mini::services {

// Adapts ApiClient's typed bot calls to BotApi.
class ApiBotApi : public BotApi
{
public:
    explicit ApiBotApi(net::ApiClient *api);

    void listBots(BotsDone done) override;
    void createBot(const net::BotCreateRequest &request, CreateDone done) override;
    void getBot(const QString &botId, BotDone done) override;
    void rotateToken(const QString &botId, TokenDone done) override;
    void deleteBot(const QString &botId, EmptyDone done) override;
    void sendMessage(const QString &botId, const QString &text, SendDone done) override;

private:
    net::ApiClient *m_api = nullptr;
};

} // namespace mini::services
