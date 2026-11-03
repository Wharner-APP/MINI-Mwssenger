#include "core/BuildInfo.h"
#include "services/BotService.h"
#include "ui/BotCreateDialog.h"
#include "ui/BotInfoDialog.h"
#include "ui/BotTokenDialog.h"
#include "ui/BotsDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

class FakeBotApi : public BotApi
{
public:
    QHash<QString, net::BotInfo> bots;
    int rotated = 0;

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
        net::BotInfo bot = make(request.login, request.displayName);
        bots.insert(bot.botId, bot);
        net::BotCreateResult result;
        result.bot = bot;
        result.token = QStringLiteral("mini_once");
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
        net::BotTokenResult result;
        result.token = QStringLiteral("mini_new_token");
        result.tokenPrefix = QStringLiteral("mini_new");
        result.tokenShownOnce = true;
        bots[botId].tokenPrefix = result.tokenPrefix;
        done(net::ApiResult<net::BotTokenResult>::ok(result));
    }

    void deleteBot(const QString &botId, EmptyDone done) override
    {
        bots.remove(botId);
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void sendMessage(const QString &, const QString &, SendDone done) override
    {
        done(net::ApiResult<net::BotSendResult>::ok(net::BotSendResult()));
    }
};

} // namespace

class TestBotUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void createDialogValidatesAndAccepts();
    void tokenDialogShowsTheTokenOnce();
    void botsDialogListsBotsAndWarnsAboutE2e();
    void botsDialogEmitsOpenForTheRowButton();
    void infoDialogRotatesAndEmitsTheNewToken();
};

void TestBotUi::createDialogValidatesAndAccepts()
{
    BotCreateDialog dialog;
    dialog.nameEdit()->setText(QStringLiteral("XY"));
    dialog.loginEdit()->setText(QStringLiteral("helper"));
    QSignalSpy accepted(&dialog, &QDialog::accepted);

    dialog.createButton()->click();
    QCOMPARE(accepted.count(), 0);
    QVERIFY(!dialog.errorLabel()->text().isEmpty());
    QVERIFY(dialog.errorLabel()->isVisibleTo(&dialog));

    dialog.nameEdit()->setText(QStringLiteral("Helper"));
    dialog.aboutEdit()->setPlainText(QStringLiteral("Answers questions"));
    dialog.groupsCheck()->setChecked(false);
    dialog.inlineCheck()->setChecked(true);
    dialog.createButton()->click();
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(dialog.normalizedLogin(), QStringLiteral("helper_bot"));
    const BotDraft draft = dialog.draft();
    QCOMPARE(draft.displayName, QStringLiteral("Helper"));
    QCOMPARE(draft.login, QStringLiteral("helper"));
    QVERIFY(!draft.canJoinGroups);
    QVERIFY(draft.inlineEnabled);
}

void TestBotUi::tokenDialogShowsTheTokenOnce()
{
    BotToken token;
    token.token = QStringLiteral("mini_abcdef");
    token.prefix = QStringLiteral("mini_abc");
    token.shownOnce = true;

    BotTokenDialog dialog(token);
    QCOMPARE(dialog.token(), QStringLiteral("mini_abcdef"));
    QCOMPARE(dialog.tokenField()->text(), QStringLiteral("mini_abcdef"));
    QVERIFY(dialog.tokenField()->isReadOnly());
    QVERIFY(dialog.warningLabel()->text().contains(QStringLiteral("not be shown again")));

    dialog.copyButton()->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("mini_abcdef"));
}

void TestBotUi::botsDialogListsBotsAndWarnsAboutE2e()
{
    FakeBotApi api;
    api.bots.insert(QStringLiteral("b_helper_bot"),
                    FakeBotApi::make(QStringLiteral("helper_bot"),
                                     QStringLiteral("Helper")));
    api.bots.insert(QStringLiteral("b_news_bot"),
                    FakeBotApi::make(QStringLiteral("news_bot"),
                                     QStringLiteral("News")));
    BotService service;
    service.setApi(&api);

    BotsDialog dialog(&service);
    QCOMPARE(dialog.botList()->count(), 2);
    QCOMPARE(dialog.bots().size(), 2);
    QVERIFY(dialog.warningLabel()->text().contains(QStringLiteral("not end-to-end encrypted")));
    QVERIFY(dialog.createButton()->isEnabled());
}

void TestBotUi::botsDialogEmitsOpenForTheRowButton()
{
    FakeBotApi api;
    api.bots.insert(QStringLiteral("b_helper_bot"),
                    FakeBotApi::make(QStringLiteral("helper_bot"),
                                     QStringLiteral("Helper")));
    BotService service;
    service.setApi(&api);

    BotsDialog dialog(&service);
    QCOMPARE(dialog.botList()->count(), 1);
    QWidget *row = dialog.botList()->itemWidget(dialog.botList()->item(0));
    QVERIFY(row != nullptr);
    auto *open = row->findChild<QPushButton *>(QStringLiteral("botOpenButton"));
    QVERIFY(open != nullptr);

    BotInfo opened;
    connect(&dialog, &BotsDialog::chatRequested, &dialog,
            [&opened](const BotInfo &bot) { opened = bot; });
    open->click();
    QCOMPARE(opened.botId, QStringLiteral("b_helper_bot"));
    QVERIFY(opened.isBot);
    QVERIFY(!opened.e2e);
}

void TestBotUi::infoDialogRotatesAndEmitsTheNewToken()
{
    FakeBotApi api;
    api.bots.insert(QStringLiteral("b_helper_bot"),
                    FakeBotApi::make(QStringLiteral("helper_bot"),
                                     QStringLiteral("Helper")));
    BotService service;
    service.setApi(&api);

    BotInfo bot;
    bot.botId = QStringLiteral("b_helper_bot");
    bot.login = QStringLiteral("helper_bot");
    bot.displayName = QStringLiteral("Helper");
    bot.about = QStringLiteral("Answers questions");
    bot.tokenPrefix = QStringLiteral("mini_pre");

    BotInfoDialog dialog(&service, bot);
    QCOMPARE(dialog.nameLabel()->text(), QStringLiteral("Helper"));
    QCOMPARE(dialog.loginLabel()->text(), QStringLiteral("@helper_bot"));
    QCOMPARE(dialog.aboutLabel()->text(), QStringLiteral("Answers questions"));

    BotToken issued;
    bool got = false;
    connect(&dialog, &BotInfoDialog::tokenIssued, &dialog,
            [&](const BotToken &token) {
                issued = token;
                got = true;
            });
    dialog.rotateButton()->click();
    QVERIFY(got);
    QCOMPARE(issued.token, QStringLiteral("mini_new_token"));
    QVERIFY(issued.shownOnce);
    QCOMPARE(api.rotated, 1);
    QVERIFY(dialog.prefixLabel()->text().contains(QStringLiteral("mini_new")));
}

QTEST_MAIN(TestBotUi)
#include "test_bot_ui.moc"
