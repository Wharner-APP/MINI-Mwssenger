#include "core/BuildInfo.h"
#include "core/Settings.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/StorageWorker.h"
#include "ui/ChatView.h"
#include "ui/Composer.h"
#include "ui/MainWindow.h"
#include "ui/MessageBubble.h"
#include "ui/Theme.h"

#include "ServiceTestSupport.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

class StubCrypto : public CryptoSession
{
public:
    Result<QByteArray> encrypt(const QString &, const QByteArray &plaintext,
                               QJsonObject *) override
    {
        return Result<QByteArray>::ok(plaintext);
    }
    Result<QByteArray> decrypt(const QString &, const QByteArray &ciphertext,
                               const QJsonObject &) override
    {
        return Result<QByteArray>::ok(ciphertext);
    }
};

class StubTransport : public MessageTransport
{
public:
    int sent = 0;
    QList<net::Envelope> envelopes;

    void send(const QList<net::Envelope> &items, SendCallback done) override
    {
        ++sent;
        envelopes.append(items);
        net::SendResult result;
        for (const net::Envelope &item : items) {
            result.accepted.append(item.clientMsgId);
        }
        if (done) {
            done(net::ApiResult<net::SendResult>::ok(result));
        }
    }
};

} // namespace

class TestMainWindowServices : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }
    void opensDirectChatByExactLoginAndSendsText();
    void closeToTrayHidesAndRestores();
    void trayMenuPersistsDoNotDisturb();
};

void TestMainWindowServices::opensDirectChatByExactLoginAndSendsText()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ChatService chat(&worker);
    chat.setSelfLogin(QStringLiteral("matvey"));
    MessageService messages(&worker);
    messages.setCancelWindowMs(0);
    StubCrypto crypto;
    StubTransport transport;
    messages.setCryptoSession(&crypto);
    messages.setTransport(&transport);

    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    MainWindow window(&settings);
    window.setChatService(&chat);
    window.setMessageService(&messages);
    window.show();

    window.openDirectChatForLogin(QStringLiteral("alice"));
    const QString chatId = ChatService::directChatId(QStringLiteral("matvey"),
                                                     QStringLiteral("alice"));
    QTRY_COMPARE_WITH_TIMEOUT(window.chatView()->chatId(), chatId, 5000);
    QVERIFY(window.chatView()->composer()->editor()->isEnabled());

    QSignalSpy added(&messages, &MessageService::messageAdded);
    Composer *composer = window.chatView()->composer();
    composer->setText(QStringLiteral("hello"));
    QTest::keyClick(composer->editor(), Qt::Key_Return);

    QTRY_VERIFY_WITH_TIMEOUT(added.count() >= 1, 5000);
    const Message created = added.first().at(0).value<Message>();
    QCOMPARE(created.body, QStringLiteral("hello"));
    QCOMPARE(created.chatId, chatId);
    QTRY_VERIFY_WITH_TIMEOUT(transport.sent >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        fetchMessage(worker, chatId, created.clientMsgId).has_value(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        fetchMessage(worker, chatId, created.clientMsgId)->state, MessageState::Sent, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(window.chatView()->messageModel()->count(), 1, 5000);
    QVERIFY(composer->editor()->toPlainText().isEmpty());
}

void TestMainWindowServices::closeToTrayHidesAndRestores()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    MainWindow window(&settings);
    window.setTrayAvailableForTesting(true);
    window.show();
    QCoreApplication::processEvents();

    QVERIFY(window.closeToTrayEnabled());
    QCOMPARE(window.trayMenu()->actions().size(), 4);
    QCOMPARE(window.trayOpenAction()->objectName(), QStringLiteral("trayOpenAction"));
    QCOMPARE(window.trayDoNotDisturbAction()->objectName(),
             QStringLiteral("trayDoNotDisturbAction"));
    QCOMPARE(window.traySignOutAction()->objectName(), QStringLiteral("traySignOutAction"));
    QCOMPARE(window.trayExitAction()->objectName(), QStringLiteral("trayExitAction"));

    QVERIFY(!window.close());
    QTRY_VERIFY(!window.isVisible());

    window.trayOpenAction()->trigger();
    QTRY_VERIFY(window.isVisible());
}

void TestMainWindowServices::trayMenuPersistsDoNotDisturb()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    Settings settings(path);
    MainWindow window(&settings);

    QVERIFY(!window.doNotDisturb());
    window.trayDoNotDisturbAction()->trigger();
    QVERIFY(window.doNotDisturb());
    QCOMPARE(settings.getBool(QStringLiteral("ui/do_not_disturb"), false), true);

    Settings reopened(path);
    QCOMPARE(reopened.getBool(QStringLiteral("ui/do_not_disturb"), false), true);
}

QTEST_MAIN(TestMainWindowServices)
#include "test_main_window_services.moc"
