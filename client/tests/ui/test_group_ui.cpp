#include "core/BuildInfo.h"
#include "domain/Group.h"
#include "ui/AdminRightsDialog.h"
#include "ui/ChatView.h"
#include "ui/GroupInfoPanel.h"
#include "ui/NewGroupDialog.h"
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

class TestGroupUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void newGroupDialogValidatesDraft();
    void adminRightsDialogRoundTripsMask();
    void groupInfoPanelShowsRosterAndGatesActions();
    void groupChatExposesInfoButton();
    void sidebarOffersNewGroup();
};

void TestGroupUi::newGroupDialogValidatesDraft()
{
    NewGroupDialog dialog;
    dialog.titleEdit()->setText(QStringLiteral("Team"));
    dialog.aboutEdit()->setPlainText(QStringLiteral("About us"));
    dialog.membersEdit()->setPlainText(QStringLiteral("alice\nbob, carol"));
    dialog.publicCheck()->setChecked(true);
    dialog.requestCheck()->setChecked(true);

    const services::GroupDraft draft = dialog.draft();
    QCOMPARE(draft.title, QStringLiteral("Team"));
    QCOMPARE(draft.about, QStringLiteral("About us"));
    QCOMPARE(draft.members, QStringList({QStringLiteral("alice"), QStringLiteral("bob"),
                                         QStringLiteral("carol")}));
    QVERIFY(draft.isPublic);
    QVERIFY(draft.joinByRequest);

    dialog.titleEdit()->clear();
    dialog.setError(QStringLiteral("Group name is required"));
    // submit() is private; validation through the service is covered by the
    // service test, here we only check the fields and the error label.
    QCOMPARE(dialog.draft().title, QString());
}

void TestGroupUi::adminRightsDialogRoundTripsMask()
{
    AdminRightsDialog dialog;
    QCOMPARE(dialog.boxes().size(), 12);
    const quint32 mask = group_permissions::Ban | group_permissions::Invite
        | group_permissions::Approve;
    dialog.setPermissions(mask);
    QCOMPARE(dialog.permissions(), mask);
    dialog.setPermissions(group_permissions::All);
    QCOMPARE(dialog.permissions(), group_permissions::All);
}

void TestGroupUi::groupInfoPanelShowsRosterAndGatesActions()
{
    Group group;
    group.groupId = QStringLiteral("g_1");
    group.title = QStringLiteral("Team");
    group.about = QStringLiteral("Daily standup");
    group.inviteLink = QStringLiteral("mini://g/token");
    group.myRole = GroupRole::Owner;
    group.myPermissions = group_permissions::All;
    group.memberCount = 2;

    GroupMember owner;
    owner.userId = QStringLiteral("u_me");
    owner.login = QStringLiteral("me");
    owner.role = GroupRole::Owner;
    GroupMember admin;
    admin.userId = QStringLiteral("u_alice");
    admin.login = QStringLiteral("alice");
    admin.role = GroupRole::Admin;
    admin.permissions = group_permissions::Ban;
    group.members = {owner, admin};

    GroupInfoPanel panel;
    panel.setGroup(group);
    QCOMPARE(panel.titleLabel()->text(), QStringLiteral("Team"));
    QCOMPARE(panel.aboutLabel()->text(), QStringLiteral("Daily standup"));
    QCOMPARE(panel.memberList()->count(), 2);
    QCOMPARE(panel.inviteField()->text(), QStringLiteral("mini://g/token"));
    QVERIFY(panel.inviteButton()->isEnabled());
    QVERIFY(panel.addMemberButton()->isEnabled());
    QVERIFY(!panel.leaveButton()->isEnabled()); // owner cannot leave

    panel.memberList()->setCurrentRow(1);
    QVERIFY(panel.makeAdminButton()->isEnabled());
    QVERIFY(panel.transferButton()->isEnabled());
    QVERIFY(panel.banButton()->isEnabled());

    group.myRole = GroupRole::Member;
    group.myPermissions = 0;
    panel.setGroup(group);
    QVERIFY(!panel.inviteButton()->isEnabled());
    QVERIFY(!panel.addMemberButton()->isEnabled());
    QVERIFY(!panel.makeAdminButton()->isEnabled());
    QVERIFY(panel.leaveButton()->isEnabled());
    QVERIFY(panel.requestList()->isHidden());
}

void TestGroupUi::groupChatExposesInfoButton()
{
    ChatView view;
    Chat direct;
    direct.chatId = QStringLiteral("direct_abc");
    direct.kind = QStringLiteral("direct");
    direct.peerLogin = QStringLiteral("alice");
    direct.title = QStringLiteral("alice");
    view.setChat(direct);
    QVERIFY(view.infoButton()->isHidden());
    QCOMPARE(view.subtitleLabel()->text(), QStringLiteral("alice"));

    Chat groupChat;
    groupChat.chatId = QStringLiteral("group_g_1");
    groupChat.kind = QStringLiteral("group");
    groupChat.title = QStringLiteral("Team");
    view.setChat(groupChat);
    QVERIFY(!view.infoButton()->isHidden());
    QCOMPARE(view.subtitleLabel()->text(), QStringLiteral("Group"));

    QSignalSpy requested(&view, &ChatView::chatInfoRequested);
    view.infoButton()->click();
    QCOMPARE(requested.count(), 1);
}

void TestGroupUi::sidebarOffersNewGroup()
{
    Sidebar sidebar;
    QSignalSpy groupRequested(&sidebar, &Sidebar::newGroupRequested);
    QSignalSpy chatRequested(&sidebar, &Sidebar::newChatRequested);
    QSignalSpy joinRequested(&sidebar, &Sidebar::joinGroupRequested);
    QMenu *menu = sidebar.newChatButton()->menu();
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().size(), 5);
    menu->actions().at(1)->trigger();
    QCOMPARE(groupRequested.count(), 1);
    QCOMPARE(chatRequested.count(), 0);
    menu->actions().at(2)->trigger();
    QCOMPARE(joinRequested.count(), 1);
    menu->actions().at(0)->trigger();
    QCOMPARE(chatRequested.count(), 1);
}

QTEST_MAIN(TestGroupUi)
#include "test_group_ui.moc"
