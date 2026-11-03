#include "services/BotService.h"

#include "services/ChatService.h"
#include "services/MessageService.h"

#include <QRegularExpression>

namespace mini::services {

namespace {

constexpr int kMinName = 3;
constexpr int kMaxName = 32;
constexpr int kMinLogin = 4;
constexpr int kMaxLogin = 24;
constexpr int kMaxAbout = 255;
constexpr auto kSuffix = "_bot";

bool hasForbiddenNameChar(const QChar character)
{
    const ushort code = character.unicode();
    if (code < 0x20 || (code >= 0x7f && code <= 0x9f)) {
        return true;
    }
    return character.category() == QChar::Other_Format;
}

} // namespace

BotService::BotService(QObject *parent) : QObject(parent) {}

QString BotService::normalizeLogin(const QString &login)
{
    QString value = login.trimmed().toLower();
    if (value.isEmpty()) {
        return value;
    }
    if (!value.endsWith(QLatin1String(kSuffix))) {
        value += QLatin1String(kSuffix);
    }
    return value;
}

bool BotService::isBotLogin(const QString &login)
{
    return login.trimmed().toLower().endsWith(QLatin1String(kSuffix));
}

Result<BotDraft> BotService::validateDraft(const BotDraft &draft)
{
    BotDraft normalized = draft;
    normalized.displayName =
        draft.displayName.normalized(QString::NormalizationForm_C).trimmed();
    if (normalized.displayName.size() < kMinName
        || normalized.displayName.size() > kMaxName) {
        return Result<BotDraft>::error(QStringLiteral("bot.name_invalid"));
    }
    for (const QChar character : normalized.displayName) {
        if (hasForbiddenNameChar(character)) {
            return Result<BotDraft>::error(QStringLiteral("bot.name_invalid"));
        }
    }

    normalized.login = normalizeLogin(draft.login);
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z][a-z0-9_]{3,23}$"));
    if (!pattern.match(normalized.login).hasMatch()
        || !normalized.login.endsWith(QLatin1String(kSuffix))
        || normalized.login.size() < kMinLogin
        || normalized.login.size() > kMaxLogin) {
        return Result<BotDraft>::error(QStringLiteral("bot.login_invalid"));
    }

    normalized.about = draft.about.trimmed();
    if (normalized.about.toUcs4().size() > kMaxAbout) {
        return Result<BotDraft>::error(QStringLiteral("bot.about_too_long"));
    }
    return Result<BotDraft>::ok(normalized);
}

QString BotService::apiError(const net::ApiError &error)
{
    if (!error.code.isEmpty()) {
        return error.code;
    }
    switch (error.kind) {
    case net::ApiErrorKind::Network:
        return QStringLiteral("network_unavailable");
    case net::ApiErrorKind::Timeout:
        return QStringLiteral("timeout");
    case net::ApiErrorKind::Unauthorized:
        return QStringLiteral("unauthorized");
    default:
        return QStringLiteral("bot_request_failed");
    }
}

BotInfo BotService::fromWire(const net::BotInfo &info)
{
    BotInfo bot;
    bot.botId = info.botId;
    bot.userId = info.userId;
    bot.login = info.login;
    bot.displayName = info.displayName;
    bot.about = info.about;
    bot.avatarMedia = info.avatarMedia;
    bot.tokenPrefix = info.tokenPrefix;
    bot.canJoinGroups = info.canJoinGroups;
    bot.canReadAllGroupMessages = info.canReadAllGroupMessages;
    bot.inlineEnabled = info.inlineEnabled;
    bot.isBot = true;
    // The server says e2e=false; keep it false even if a future payload omits it.
    bot.e2e = false;
    bot.createdAt = info.createdAt;
    bot.updatedAt = info.updatedAt;
    return bot;
}

void BotService::listBots(Done<QVector<BotInfo>> done)
{
    if (!m_api) {
        done(Result<QVector<BotInfo>>::error(QStringLiteral("bots_unavailable")));
        return;
    }
    m_api->listBots([done = std::move(done)](net::ApiResult<QList<net::BotInfo>> result) {
        if (result.isError()) {
            done(Result<QVector<BotInfo>>::error(apiError(result.error())));
            return;
        }
        QVector<BotInfo> bots;
        bots.reserve(result.value().size());
        for (const net::BotInfo &info : result.value()) {
            bots.append(fromWire(info));
        }
        done(Result<QVector<BotInfo>>::ok(bots));
    });
}

void BotService::createBot(const BotDraft &draft, Done<BotCreateResult> done)
{
    const Result<BotDraft> valid = validateDraft(draft);
    if (valid.isError()) {
        done(Result<BotCreateResult>::error(valid.error()));
        return;
    }
    if (!m_api) {
        done(Result<BotCreateResult>::error(QStringLiteral("bots_unavailable")));
        return;
    }
    net::BotCreateRequest request;
    request.login = valid.value().login;
    request.displayName = valid.value().displayName;
    request.about = valid.value().about;
    request.canJoinGroups = valid.value().canJoinGroups;
    request.inlineEnabled = valid.value().inlineEnabled;
    m_api->createBot(
        request, [this, done = std::move(done)](net::ApiResult<net::BotCreateResult> result) {
            if (result.isError()) {
                done(Result<BotCreateResult>::error(apiError(result.error())));
                return;
            }
            BotCreateResult created;
            created.bot = fromWire(result.value().bot);
            created.token.token = result.value().token;
            created.token.prefix = created.bot.tokenPrefix;
            created.token.shownOnce = result.value().tokenShownOnce;
            emit botsChanged();
            emit botCreated(created.bot);
            // The token is handed to the caller and deliberately not kept.
            done(Result<BotCreateResult>::ok(created));
        });
}

void BotService::rotateToken(const QString &botId, Done<BotToken> done)
{
    if (botId.trimmed().isEmpty()) {
        done(Result<BotToken>::error(QStringLiteral("bot.id_required")));
        return;
    }
    if (!m_api) {
        done(Result<BotToken>::error(QStringLiteral("bots_unavailable")));
        return;
    }
    m_api->rotateToken(
        botId, [done = std::move(done)](net::ApiResult<net::BotTokenResult> result) {
            if (result.isError()) {
                done(Result<BotToken>::error(apiError(result.error())));
                return;
            }
            BotToken token;
            token.token = result.value().token;
            token.prefix = result.value().tokenPrefix;
            token.shownOnce = result.value().tokenShownOnce;
            done(Result<BotToken>::ok(token));
        });
}

void BotService::deleteBot(const QString &botId, Done<bool> done)
{
    if (botId.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("bot.id_required")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("bots_unavailable")));
        return;
    }
    m_api->deleteBot(botId, [this, botId, done = std::move(done)](
                                 net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        emit botsChanged();
        emit botDeleted(botId);
        done(Result<bool>::ok(true));
    });
}

void BotService::openChat(const BotInfo &bot, Done<Chat> done)
{
    if (!isBotLogin(bot.login)) {
        done(Result<Chat>::error(QStringLiteral("bot.login_invalid")));
        return;
    }
    if (!m_chats) {
        done(Result<Chat>::error(QStringLiteral("chat_unavailable")));
        return;
    }
    m_chats->openDirectChat(
        bot.login, [bot, done = std::move(done)](Result<Chat> result) {
            if (result.isError()) {
                done(result);
                return;
            }
            Chat chat = result.value();
            chat.isBot = true;
            chat.e2e = false;
            if (!bot.displayName.isEmpty()) {
                chat.title = bot.displayName;
            }
            done(Result<Chat>::ok(chat));
        });
}

void BotService::sendText(const QString &botId, const QString &chatId, const QString &text,
                          Done<bool> done)
{
    if (botId.trimmed().isEmpty() || chatId.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("bot.chat_required")));
        return;
    }
    if (text.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("empty_message")));
        return;
    }
    if (text.toUcs4().size() > 4096) {
        done(Result<bool>::error(QStringLiteral("message_too_long")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("bots_unavailable")));
        return;
    }
    m_api->sendMessage(
        botId, text, [this, chatId, text, done = std::move(done)](
                         net::ApiResult<net::BotSendResult> result) {
            if (result.isError()) {
                done(Result<bool>::error(apiError(result.error())));
                return;
            }
            if (!m_messages) {
                done(Result<bool>::error(QStringLiteral("storage_unavailable")));
                return;
            }
            const QString id = m_messages->recordPlainText(
                chatId, text, result.value().messageId);
            if (id.isEmpty()) {
                done(Result<bool>::error(QStringLiteral("message_store_failed")));
                return;
            }
            done(Result<bool>::ok(true));
        });
}

} // namespace mini::services
