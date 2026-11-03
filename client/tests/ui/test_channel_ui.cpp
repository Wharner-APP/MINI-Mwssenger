#include "core/BuildInfo.h"
#include "domain/Channel.h"
#include "ui/ChannelInfoPanel.h"
#include "ui/ChatView.h"
#include "ui/NewChannelDialog.h"
#include "ui/Sidebar.h"
#include "ui/Theme.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using namespace mini;

class TestChannelUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void newChannelDialogRoundTripsDraft();
    void channelInfoPanelGatesActionsByRole();
    void channelChatDisablesComposerForEveryone();
    void sidebarOffersNewChannel();
};

void TestChannelUi::newChannelDialogRoundTripsDraft()
{
    NewChannelDialog dialog;
    dialog.titleEdit()->setText(QStringLiteral("News"));
    dialog.aboutEdit()->setPlainText(QStringLiteral("Daily updates"));
    dialog.publicCheck()->setChecked(true);

    const services::ChannelDraft draft = dialog.draft();
    QCOMPARE(draft.title, QStringLiteral("News"));
    QCOMPARE(draft.about, QStringLiteral("Daily updates"));
    QVERIFY(draft.isPublic);

    dialog.titleEdit()->clear();
    dialog.setError(QStringLiteral("Channel name is required"));
    QVERIFY(dialog.draft().title.isEmpty());
}

void TestChannelUi::channelInfoPanelGatesActionsByRole()
{
    Channel channel;
    channel.channelId = QStringLiteral("c_1");
    channel.title = QStringLiteral("News");
    channel.about = QStringLiteral("Daily updates");
    channel.inviteLink = QStringLiteral("mini://c/c_1/tok");
    channel.myRole = ChannelRole::Owner;
    channel.isMember = true;
    channel.myPermissions = channel_permissions::All;
    channel.subscriberCount = 2;

    ChannelMember owner;
    owner.userId = QStringLiteral("u_me");
    owner.login = QStringLiteral("me");
    owner.role = ChannelRole::Owner;
    ChannelMember reader;
    reader.userId = QStringLiteral("u_alice");
    reader.login = QStringLiteral("alice");
    reader.role = ChannelRole::Subscriber;
    channel.members = {owner, reader};

    ChannelInfoPanel panel;
    panel.setChannel(channel);
    QCOMPARE(panel.titleLabel()->text(), QStringLiteral("News"));
    QCOMPARE(panel.aboutLabel()->text(), QStringLiteral("Daily updates"));
    QCOMPARE(panel.memberList()->count(), 2);
    QCOMPARE(panel.inviteField()->text(), QStringLiteral("mini://c/c_1/tok"));
    QVERIFY(panel.inviteButton()->isEnabled());
    QVERIFY(panel.deleteButton()->isEnabled());
    QVERIFY(!panel.unsubscribeButton()->isEnabled()); // owner cannot leave

    panel.memberList()->setCurrentRow(1);
    QVERIFY(panel.makeAdminButton()->isEnabled());
    QVERIFY(panel.makeSubscriberButton()->isEnabled());

    channel.myRole = ChannelRole::Subscriber;
    channel.isMember = true;
    channel.myPermissions = 0;
    panel.setChannel(channel);
    QVERIFY(!panel.inviteButton()->isEnabled());
    QVERIFY(!panel.makeAdminButton()->isEnabled());
    QVERIFY(!panel.deleteButton()->isEnabled());
    QVERIFY(panel.unsubscribeButton()->isEnabled());
}

void TestChannelUi::channelChatDisablesComposerForEveryone()
{
    ChatView view;
    Chat channel;
    channel.chatId = QStringLiteral("channel_c_1");
    channel.kind = QStringLiteral("channel");
    channel.title = QStringLiteral("News");
    view.setChat(channel);

    QVERIFY(!view.infoButton()->isHidden());
    QCOMPARE(view.subtitleLabel()->text(), QStringLiteral("Channel"));
    QVERIFY(!view.composerEnabled());

    // The window enables it only after the channel role says "publisher".
    view.setComposerEnabled(true);
    QVERIFY(view.composerEnabled());

    Chat group;
    group.chatId = QStringLiteral("group_g_1");
    group.kind = QStringLiteral("group");
    group.title = QStringLiteral("Team");
    view.setChat(group);
    QVERIFY(view.composerEnabled());
    QCOMPARE(view.subtitleLabel()->text(), QStringLiteral("Group"));

    QSignalSpy requested(&view, &ChatView::chatInfoRequested);
    view.infoButton()->click();
    QCOMPARE(requested.count(), 1);
}

void TestChannelUi::sidebarOffersNewChannel()
{
    Sidebar sidebar;
    QSignalSpy channelRequested(&sidebar, &Sidebar::newChannelRequested);
    QSignalSpy joinChannelRequested(&sidebar, &Sidebar::joinChannelRequested);
    QMenu *menu = sidebar.newChatButton()->menu();
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().size(), 5);
    menu->actions().at(3)->trigger();
    QCOMPARE(channelRequested.count(), 1);
    menu->actions().at(4)->trigger();
    QCOMPARE(joinChannelRequested.count(), 1);
}

QTEST_MAIN(TestChannelUi)
#include "test_channel_ui.moc"
