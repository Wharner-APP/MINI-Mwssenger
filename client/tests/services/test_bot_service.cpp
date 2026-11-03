#include "services/BotService.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

class FakeBotApi : public BotApi
{
public:
    QHash<QString, net::BotInfo> bots;
    int created = 0;
    int rotated = 0;
    int deleted = 0;
    int sent = 0;
    QStringList tokens;

    static net::BotInfo make(const QString &login, const QString &name)
    {
        net::BotInfo bot;
        bot.botId = QStringLiteral("b_") + login;
        bot.userId = QStringLiteral("u_") + login;
        bot.login = login;
        bot.displayName = name;
        bot.tokenPrefix = QStringLiteral("mini_pre");
        bot.e2e = false;
        bot.isBot = true;
        bot.createdAt = 1000;
        bot.updatedAt = 1000;
        return bot;
    }

    void listBots(BotsDone done) override
    {
        QList<net::BotInfo> list;
        for (const net::BotInfo &bot : bots) {
            list.append(bot);
        }
        done(net::ApiResult<QList<net::BotInfo>>::ok(list));
    }

    void createBot(const net::BotCreateRequest &request, CreateDone done) override
    {
        ++created;
        net::BotInfo bot = make(request.login, request.displayName);
        bot.about = request.about;
        bot.canJoinGroups = request.canJoinGroups;
        bot.inlineEnabled = request.inlineEnabled;
        bots.insert(bot.botId, bot);
        const QString token = QStringLiteral("mini_test_%1").arg(created);
        tokens.append(token);
        net::BotCreateResult result;
        result.bot = bot;
        result.token = token;
        result.tokenShownOnce = true;
        done(net::ApiResult<net::BotCreateResult>::ok(result));
    }

    void getBot(const QString &botId, BotDone done) override
    {
        done(net::ApiResult<net::BotInfo>::ok(bots.value(botId)));
    }

    void rotateToken(const QString &botId, TokenDone done) override
    {
        ++rotated;
        const QString token = QStringLiteral("mini_rotated_%1").arg(rotated);
        tokens.append(token);
        net::BotTokenResult result;
        result.token = token;
        result.tokenPrefix = token.left(8);
        result.tokenShownOnce = true;
        bots[botId].tokenPrefix = result.tokenPrefix;
        done(net::ApiResult<net::BotTokenResult>::ok(result));
    }

    void deleteBot(const QString &botId, EmptyDone done) override
    {
        ++deleted;
        bots.remove(botId);
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void sendMessage(const QString &, const QString &, SendDone done) override
    {
        ++sent;
        net::BotSendResult result;
        result.messageId = QStringLiteral("m_bot_1");
        result.updateId = QStringLiteral("42");
        done(net::ApiResult<net::BotSendResult>::ok(result));
    }
};

class StubTransport : public MessageTransport
{
public:
    void send(const QList<net::Envelope> &, SendCallback done) override
    {
        if (done) {
            done(net::ApiResult<net::SendResult>::ok(net::SendResult()));
        }
    }

    void deliver(const net::Envelope &envelope) { emit messageReceived(envelope); }
};

} // namespace

class TestBotService : public QObject
{
    Q_OBJECT

private slots:
    void validatesDraftAndSuffix();
    void createsAndListsBotsWithoutKeepingTheToken();
    void rotatesTokenEveryTime();
    void deletesBot();
    void opensBotChatWithLabels();
    void sendsPlainTextAndRecordsLocalRow();
    void receivesPlaintextBotReply();
};

void TestBotService::validatesDraftAndSuffix()
{
    BotDraft draft;
    draft.displayName = QStringLiteral("Helper");
    draft.login = QStringLiteral("helper");
    auto valid = BotService::validateDraft(draft);
    QVERIFY(valid.isOk());
    QCOMPARE(valid.value().login, QStringLiteral("helper_bot"));
    QCOMPARE(BotService::normalizeLogin(QStringLiteral("Helper_BOT")),
             QStringLiteral("helper_bot"));

    draft.displayName = QStringLiteral("XY");
    QVERIFY(BotService::validateDraft(draft).isError());

    draft.displayName = QStringLiteral("Helper");
    draft.login = QStringLiteral("1bad");
    QVERIFY(BotService::validateDraft(draft).isError());

    draft.login = QStringLiteral("this_login_is_far_too_long_for_a_bot");
    QVERIFY(BotService::validateDraft(draft).isError());

    draft.login = QStringLiteral("helper");
    draft.about = QString(300, QLatin1Char('a'));
    QVERIFY(BotService::validateDraft(draft).isError());

    QVERIFY(BotService::isBotLogin(QStringLiteral("helper_bot")));
    QVERIFY(!BotService::isBotLogin(QStringLiteral("helper")));
}

void TestBotService::createsAndListsBotsWithoutKeepingTheToken()
{
    FakeBotApi api;
    BotService service;
    service.setApi(&api);

    BotDraft draft;
    draft.displayName = QStringLiteral("Helper");
    draft.login = QStringLiteral("helper");
    draft.about = QStringLiteral("Answers questions");
    draft.inlineEnabled = true;

    QSignalSpy created(&service, &BotService::botCreated);
    BotCreateResult createdResult;
    bool finished = false;
    service.createBot(draft, [&](Result<BotCreateResult> result) {
        finished = true;
        if (result.isOk()) {
            createdResult = result.value();
        }
    });
    QVERIFY(finished);
    QCOMPARE(created.count(), 1);
    QCOMPARE(createdResult.bot.login, QStringLiteral("helper_bot"));
    QVERIFY(createdResult.bot.isBot);
    QVERIFY(!createdResult.bot.e2e);
    QVERIFY(createdResult.token.shownOnce);
    QVERIFY(createdResult.token.token.startsWith(QStringLiteral("mini_")));
    QCOMPARE(createdResult.token.prefix, QStringLiteral("mini_pre"));

    QVector<BotInfo> listed;
    service.listBots([&](Result<QVector<BotInfo>> result) {
        if (result.isOk()) {
            listed = result.value();
        }
    });
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().login, QStringLiteral("helper_bot"));
    // BotInfo has no token field at all: the plaintext cannot leak from a list.
    QCOMPARE(listed.first().tokenPrefix, QStringLiteral("mini_pre"));
    QCOMPARE(api.tokens.size(), 1);
}

void TestBotService::rotatesTokenEveryTime()
{
    FakeBotApi api;
    api.bots.insert(QStringLiteral("b_helper_bot"),
                    FakeBotApi::make(QStringLiteral("helper_bot"),
                                     QStringLiteral("Helper")));
    BotService service;
    service.setApi(&api);

    QString first;
    QString second;
    service.rotateToken(QStringLiteral("b_helper_bot"),
                        [&](Result<BotToken> result) {
                            if (result.isOk()) {
                                first = result.value().token;
                            }
                        });
    service.rotateToken(QStringLiteral("b_helper_bot"),
                        [&](Result<BotToken> result) {
                            if (result.isOk()) {
                                second = result.value().token;
                            }
                        });
    QCOMPARE(api.rotated, 2);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(first != second);
    // Each call shows a new token once; nothing is cached for later screens.
    QCOMPARE(api.tokens.size(), 2);
}

void TestBotService::deletesBot()
{
    FakeBotApi api;
    api.bots.insert(QStringLiteral("b_helper_bot"),
                    FakeBotApi::make(QStringLiteral("helper_bot"),
                                     QStringLiteral("Helper")));
    BotService service;
    service.setApi(&api);

    QSignalSpy deleted(&service, &BotService::botDeleted);
    bool ok = false;
    service.deleteBot(QStringLiteral("b_helper_bot"),
                      [&](Result<bool> result) { ok = result.isOk(); });
    QVERIFY(ok);
    QCOMPARE(api.deleted, 1);
    QVERIFY(api.bots.isEmpty());
    QCOMPARE(deleted.count(), 1);
}

void TestBotService::opensBotChatWithLabels()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());
    ChatService chats(&worker);
    chats.setSelfLogin(QStringLiteral("matvey"));

    FakeBotApi api;
    BotService service;
    service.setApi(&api);
    service.setChatService(&chats);

    BotInfo bot;
    bot.botId = QStringLiteral("b_helper_bot");
    bot.login = QStringLiteral("helper_bot");
    bot.displayName = QStringLiteral("Helper");
    bot.isBot = true;
    bot.e2e = false;

    Chat chat;
    service.openChat(bot, [&](Result<Chat> result) {
        if (result.isOk()) {
            chat = result.value();
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(!chat.chatId.isEmpty(), 5000);
    QVERIFY(chat.isBot);
    QVERIFY(!chat.e2e);
    QCOMPARE(chat.title, QStringLiteral("Helper"));
    QCOMPARE(chat.chatId,
             ChatService::directChatId(QStringLiteral("matvey"),
                                       QStringLiteral("helper_bot")));

    // The label is derived from the login when the row is read back.
    auto stored = fetchChat(worker, chat.chatId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->isBot);
    QVERIFY(!stored->e2e);

    // Opening the same kind of login through ChatService directly must also
    // mark the chat, otherwise the E2E pipeline would be used by mistake.
    Chat direct;
    chats.openDirectChat(QStringLiteral("news_bot"),
                         [&](Result<Chat> result) {
                             if (result.isOk()) {
                                 direct = result.value();
                             }
                         });
    QTRY_VERIFY_WITH_TIMEOUT(!direct.chatId.isEmpty(), 5000);
    QVERIFY(direct.isBot);
    QVERIFY(!direct.e2e);
}

void TestBotService::sendsPlainTextAndRecordsLocalRow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());
    ChatService chats(&worker);
    chats.setSelfLogin(QStringLiteral("matvey"));
    const bool seeded = seedChat(worker, QStringLiteral("direct_1"),
                                 QStringLiteral("helper_bot"));
    QVERIFY(seeded);

    MessageService messages(&worker);
    messages.setSelfLogin(QStringLiteral("matvey"));

    FakeBotApi api;
    BotService service;
    service.setApi(&api);
    service.setChatService(&chats);
    service.setMessageService(&messages);

    QSignalSpy added(&messages, &MessageService::messageAdded);
    bool sent = false;
    service.sendText(QStringLiteral("b_helper_bot"), QStringLiteral("direct_1"),
                     QStringLiteral("hello bot"),
                     [&](Result<bool> result) { sent = result.isOk(); });
    QVERIFY(sent);
    QCOMPARE(api.sent, 1);
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    const Message stored = added.first().at(0).value<Message>();
    QCOMPARE(stored.body, QStringLiteral("hello bot"));
    QCOMPARE(stored.state, MessageState::Sent);
    QVERIFY(stored.outgoing);
    QCOMPARE(stored.serverMsgId, QStringLiteral("m_bot_1"));
}

void TestBotService::receivesPlaintextBotReply()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    MessageService messages(&worker);
    messages.setSelfLogin(QStringLiteral("matvey"));
    StubTransport transport;
    messages.setTransport(&transport);

    QSignalSpy added(&messages, &MessageService::messageAdded);
    net::Envelope envelope;
    envelope.kind = QStringLiteral("bot");
    envelope.clientMsgId = QStringLiteral("m_bot_reply");
    envelope.chatId = QStringLiteral("u_matvey"); // account id, not the chat id
    envelope.ciphertext = QByteArrayLiteral("hello from the bot");
    envelope.header.insert(QStringLiteral("bot"), true);
    envelope.header.insert(QStringLiteral("bot_login"),
                           QStringLiteral("helper_bot"));
    envelope.header.insert(QStringLiteral("bot_display_name"),
                           QStringLiteral("Helper"));
    envelope.header.insert(QStringLiteral("sender_login"),
                           QStringLiteral("helper_bot"));
    transport.deliver(envelope);

    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    const Message stored = added.first().at(0).value<Message>();
    QCOMPARE(stored.body, QStringLiteral("hello from the bot"));
    QCOMPARE(stored.senderLogin, QStringLiteral("helper_bot"));
    QVERIFY(!stored.outgoing);
    const QString chatId =
        ChatService::directChatId(QStringLiteral("matvey"), QStringLiteral("helper_bot"));
    QCOMPARE(stored.chatId, chatId);

    auto chat = fetchChat(worker, chatId);
    QVERIFY(chat.has_value());
    QVERIFY(chat->isBot);
    QVERIFY(!chat->e2e);
    QCOMPARE(chat->title, QStringLiteral("Helper"));
}

QTEST_MAIN(TestBotService)
#include "test_bot_service.moc"
