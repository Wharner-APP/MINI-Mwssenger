#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QList>
#include <QString>

#include <functional>

namespace mini::services {

// Network seam of BotService (§30.5.4, §44). Production uses ApiBotApi;
// tests implement it in memory without a server. The token travels only in
// create/rotate results: the client never stores it.
class BotApi
{
public:
    virtual ~BotApi() = default;

    using BotsDone = std::function<void(net::ApiResult<QList<net::BotInfo>>)>;
    using BotDone = std::function<void(net::ApiResult<net::BotInfo>)>;
    using CreateDone = std::function<void(net::ApiResult<net::BotCreateResult>)>;
    using TokenDone = std::function<void(net::ApiResult<net::BotTokenResult>)>;
    using EmptyDone = std::function<void(net::ApiResult<net::Empty>)>;
    using SendDone = std::function<void(net::ApiResult<net::BotSendResult>)>;

    virtual void listBots(BotsDone done) = 0;
    virtual void createBot(const net::BotCreateRequest &request, CreateDone done) = 0;
    virtual void getBot(const QString &botId, BotDone done) = 0;
    virtual void rotateToken(const QString &botId, TokenDone done) = 0;
    virtual void deleteBot(const QString &botId, EmptyDone done) = 0;
    // Plaintext by design (§29.14): the bot owner can read this message.
    virtual void sendMessage(const QString &botId, const QString &text, SendDone done) = 0;
};

} // namespace mini::services
