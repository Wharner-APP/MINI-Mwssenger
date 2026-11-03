#pragma once

#include "core/Result.h"
#include "domain/Chat.h"
#include "services/BotApi.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::services {

class ChatService;
class MessageService;

// Everything S25 collects. The login is the part before the automatic `_bot`
// suffix; the service normalizes and validates it against the server rules
// (§28.2, §44.1). The display name follows S25's 3..32 range.
struct BotDraft
{
    QString displayName;
    QString login;
    QString about;
    bool canJoinGroups = true;
    bool inlineEnabled = false;
};

struct BotInfo
{
    QString botId;
    QString userId;
    QString login;
    QString displayName;
    QString about;
    QString avatarMedia;
    QString tokenPrefix;
    bool canJoinGroups = true;
    bool canReadAllGroupMessages = false;
    bool inlineEnabled = false;
    bool isBot = true;
    // §29.14: a bot reads the message, so a chat with it is never E2E.
    bool e2e = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

// The only moment the plaintext token exists on the client. It is returned in
// this value and never stored by the service (§44.1).
struct BotToken
{
    QString token;
    QString prefix;
    bool shownOnce = true;
};

struct BotCreateResult
{
    BotInfo bot;
    BotToken token;
};

// Owner-side bot lifecycle and the "open chat" action. The bot itself talks to
// `/bot/v1`; this client only manages its metadata and token.
class BotService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit BotService(QObject *parent = nullptr);

    void setApi(BotApi *api) { m_api = api; }
    void setChatService(ChatService *chats) { m_chats = chats; }
    void setMessageService(MessageService *messages) { m_messages = messages; }
    [[nodiscard]] bool hasApi() const { return m_api != nullptr; }

    // Appends `_bot` when absent, lowercases and validates. Errors:
    // "bot.name_invalid", "bot.login_invalid", "bot.about_too_long".
    [[nodiscard]] static Result<BotDraft> validateDraft(const BotDraft &draft);
    [[nodiscard]] static QString normalizeLogin(const QString &login);
    [[nodiscard]] static bool isBotLogin(const QString &login);

    void listBots(Done<QVector<BotInfo>> done);
    void createBot(const BotDraft &draft, Done<BotCreateResult> done);
    void rotateToken(const QString &botId, Done<BotToken> done);
    void deleteBot(const QString &botId, Done<bool> done);
    // Opens (or finds) the direct chat with the bot and marks it as a bot
    // chat that is not end-to-end encrypted.
    void openChat(const BotInfo &bot, Done<Chat> done);
    // Sends plaintext to the bot (§29.14). The local history row is recorded
    // by MessageService, so the UI updates through messageAdded as usual.
    void sendText(const QString &botId, const QString &chatId, const QString &text,
                  Done<bool> done);

signals:
    void botsChanged();
    void botCreated(const mini::services::BotInfo &bot);
    void botDeleted(const QString &botId);

private:
    static BotInfo fromWire(const net::BotInfo &info);
    static QString apiError(const net::ApiError &error);

    BotApi *m_api = nullptr;
    ChatService *m_chats = nullptr;
    MessageService *m_messages = nullptr;
};

} // namespace mini::services

Q_DECLARE_METATYPE(mini::services::BotInfo)
