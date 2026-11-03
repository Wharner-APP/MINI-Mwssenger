#include "ui/GroupInfoPanel.h"

#include "services/GroupService.h"
#include "ui/AdminRightsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace mini {
namespace {

constexpr int kUserIdRole = Qt::UserRole;
constexpr int kLoginRole = Qt::UserRole + 1;
constexpr int kRoleRole = Qt::UserRole + 2;

QString roleLabel(GroupRole role)
{
    switch (role) {
    case GroupRole::Owner:
        return QObject::tr("Owner");
    case GroupRole::Admin:
        return QObject::tr("Admin");
    case GroupRole::Member:
        break;
    }
    return QObject::tr("Member");
}

} // namespace

GroupInfoPanel::GroupInfoPanel(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("groupInfoPanel"));
    setWindowTitle(tr("Group information"));
    setModal(false);
    resize(420, 620);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("groupInfoTitle"));
    m_title->setProperty("miniRole", QStringLiteral("title"));
    m_title->setWordWrap(true);

    m_memberCount = new QLabel(this);
    m_memberCount->setObjectName(QStringLiteral("groupInfoMemberCount"));
    m_memberCount->setProperty("miniRole", QStringLiteral("caption"));

    m_about = new QLabel(this);
    m_about->setObjectName(QStringLiteral("groupInfoAbout"));
    m_about->setWordWrap(true);

    m_invite = new QLineEdit(this);
    m_invite->setObjectName(QStringLiteral("groupInviteField"));
    m_invite->setReadOnly(true);
    m_invite->setPlaceholderText(tr("Invite link"));
    m_inviteButton = new QPushButton(tr("Invite"), this);
    m_inviteButton->setObjectName(QStringLiteral("groupInviteButton"));

    m_members = new QListWidget(this);
    m_members->setObjectName(QStringLiteral("groupMemberList"));
    m_members->setMinimumHeight(150);

    m_addButton = new QPushButton(tr("Add member"), this);
    m_addButton->setObjectName(QStringLiteral("groupAddMemberButton"));
    m_adminButton = new QPushButton(tr("Admin rights"), this);
    m_adminButton->setObjectName(QStringLiteral("groupAdminButton"));
    m_memberButton = new QPushButton(tr("Make member"), this);
    m_memberButton->setObjectName(QStringLiteral("groupMemberButton"));
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setObjectName(QStringLiteral("groupRemoveButton"));
    m_banButton = new QPushButton(tr("Ban"), this);
    m_banButton->setObjectName(QStringLiteral("groupBanButton"));
    m_transferButton = new QPushButton(tr("Transfer ownership"), this);
    m_transferButton->setObjectName(QStringLiteral("groupTransferButton"));

    m_requests = new QListWidget(this);
    m_requests->setObjectName(QStringLiteral("groupRequestList"));
    m_requests->setMaximumHeight(120);
    m_approveButton = new QPushButton(tr("Approve"), this);
    m_approveButton->setObjectName(QStringLiteral("groupApproveButton"));
    m_rejectButton = new QPushButton(tr("Reject"), this);
    m_rejectButton->setObjectName(QStringLiteral("groupRejectButton"));

    m_leaveButton = new QPushButton(tr("Leave group"), this);
    m_leaveButton->setObjectName(QStringLiteral("groupLeaveButton"));

    auto *inviteRow = new QHBoxLayout();
    inviteRow->addWidget(m_invite, 1);
    inviteRow->addWidget(m_inviteButton);

    auto *memberActions = new QHBoxLayout();
    memberActions->addWidget(m_addButton);
    memberActions->addWidget(m_adminButton);
    memberActions->addWidget(m_memberButton);
    memberActions->addWidget(m_removeButton);
    memberActions->addWidget(m_banButton);
    memberActions->addWidget(m_transferButton);

    auto *requestActions = new QHBoxLayout();
    requestActions->addWidget(m_approveButton);
    requestActions->addWidget(m_rejectButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    layout->addWidget(m_title);
    layout->addWidget(m_memberCount);
    layout->addWidget(m_about);
    layout->addLayout(inviteRow);
    layout->addWidget(new QLabel(tr("Members"), this));
    layout->addWidget(m_members, 1);
    layout->addLayout(memberActions);
    layout->addWidget(new QLabel(tr("Join requests"), this));
    layout->addWidget(m_requests);
    layout->addLayout(requestActions);
    layout->addWidget(m_leaveButton);
    layout->addWidget(buttons);

    connect(m_inviteButton, &QPushButton::clicked, this, &GroupInfoPanel::createInvite);
    connect(m_addButton, &QPushButton::clicked, this, &GroupInfoPanel::addMember);
    connect(m_adminButton, &QPushButton::clicked, this, &GroupInfoPanel::makeAdmin);
    connect(m_memberButton, &QPushButton::clicked, this, &GroupInfoPanel::makeMember);
    connect(m_removeButton, &QPushButton::clicked, this, &GroupInfoPanel::removeSelected);
    connect(m_banButton, &QPushButton::clicked, this, &GroupInfoPanel::banSelected);
    connect(m_transferButton, &QPushButton::clicked, this, &GroupInfoPanel::transferSelected);
    connect(m_approveButton, &QPushButton::clicked, this,
            [this] { approveSelected(true); });
    connect(m_rejectButton, &QPushButton::clicked, this,
            [this] { approveSelected(false); });
    connect(m_leaveButton, &QPushButton::clicked, this, &GroupInfoPanel::leaveGroup);
    connect(m_members, &QListWidget::itemSelectionChanged, this,
            &GroupInfoPanel::updatePermissions);
}

void GroupInfoPanel::setGroupService(services::GroupService *service)
{
    m_service = service;
}

void GroupInfoPanel::setGroup(const Group &group)
{
    m_group = group;
    rebuild();
}

void GroupInfoPanel::refresh()
{
    if (!m_service || m_group.groupId.isEmpty()) {
        return;
    }
    const QString groupId = m_group.groupId;
    m_service->loadGroup(groupId, [this, groupId](Result<Group> result) {
        if (result.isOk() && result.value().groupId == groupId) {
            setGroup(result.value());
            if (m_service && m_group.can(group_permissions::Approve)) {
                m_service->listRequests(
                    groupId, [this, groupId](Result<QVector<GroupJoinRequest>> requests) {
                        if (m_group.groupId != groupId || requests.isError()) {
                            return;
                        }
                        m_requests->clear();
                        for (const GroupJoinRequest &request : requests.value()) {
                            auto *item = new QListWidgetItem(
                                QStringLiteral("%1 — %2")
                                    .arg(request.login.isEmpty() ? request.userId
                                                                 : request.login,
                                         request.message));
                            item->setData(kUserIdRole, request.userId);
                            item->setData(kLoginRole, request.login);
                            m_requests->addItem(item);
                        }
                    });
            }
        }
    });
}

void GroupInfoPanel::rebuild()
{
    m_title->setText(m_group.title.isEmpty() ? m_group.groupId : m_group.title);
    m_about->setText(m_group.about);
    m_about->setVisible(!m_group.about.isEmpty());
    m_memberCount->setText(tr("%1 members").arg(m_group.memberCount));
    m_invite->setText(m_group.inviteLink.isEmpty() && !m_group.inviteToken.isEmpty()
                          ? QStringLiteral("mini://g/") + m_group.inviteToken
                          : m_group.inviteLink);

    m_members->clear();
    for (const GroupMember &member : m_group.members) {
        const QString name = member.displayName.isEmpty()
            ? (member.login.isEmpty() ? member.userId : member.login)
            : QStringLiteral("%1 (%2)").arg(member.displayName, member.login);
        auto *item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(name,
                                                                        roleLabel(member.role)));
        item->setData(kUserIdRole, member.userId);
        item->setData(kLoginRole, member.login);
        item->setData(kRoleRole, groupRoleToString(member.role));
        m_members->addItem(item);
    }
    updatePermissions();
}

void GroupInfoPanel::updatePermissions()
{
    const bool canInvite = m_group.can(group_permissions::Invite);
    const bool canBan = m_group.can(group_permissions::Ban);
    const bool canAssign = m_group.can(group_permissions::AssignAdmins);
    const bool canApprove = m_group.can(group_permissions::Approve);
    const bool hasSelection = m_members->currentItem() != nullptr;
    const GroupRole role = selectedMemberRole();

    m_inviteButton->setEnabled(canInvite);
    m_addButton->setEnabled(canInvite);
    m_adminButton->setEnabled(canAssign && hasSelection && role != GroupRole::Owner);
    m_memberButton->setEnabled(canAssign && hasSelection && role == GroupRole::Admin);
    m_removeButton->setEnabled(canBan && hasSelection && role != GroupRole::Owner);
    m_banButton->setEnabled(canBan && hasSelection && role != GroupRole::Owner);
    m_transferButton->setEnabled(m_group.isOwner() && hasSelection && role == GroupRole::Admin);
    m_approveButton->setEnabled(canApprove);
    m_rejectButton->setEnabled(canApprove);
    m_requests->setVisible(canApprove);
    m_approveButton->setVisible(canApprove);
    m_rejectButton->setVisible(canApprove);
    if (!canApprove) {
        m_requests->clear();
    }
    m_leaveButton->setEnabled(!m_group.isOwner());
}

QString GroupInfoPanel::selectedMemberId() const
{
    const QListWidgetItem *item = m_members->currentItem();
    return item ? item->data(kUserIdRole).toString() : QString();
}

QString GroupInfoPanel::selectedMemberLogin() const
{
    const QListWidgetItem *item = m_members->currentItem();
    return item ? item->data(kLoginRole).toString() : QString();
}

QString GroupInfoPanel::selectedRequestId() const
{
    const QListWidgetItem *item = m_requests->currentItem();
    return item ? item->data(kUserIdRole).toString() : QString();
}

GroupRole GroupInfoPanel::selectedMemberRole() const
{
    const QListWidgetItem *item = m_members->currentItem();
    return item ? groupRoleFromString(item->data(kRoleRole).toString())
                : GroupRole::Member;
}

void GroupInfoPanel::addMember()
{
    if (!m_service) {
        return;
    }
    bool accepted = false;
    const QString login = QInputDialog::getText(this, tr("Add member"), tr("Exact login"),
                                                QLineEdit::Normal, QString(), &accepted)
                              .trimmed();
    if (!accepted || login.isEmpty()) {
        return;
    }
    m_service->addMember(m_group.groupId, login, [this](Result<GroupMember> result) {
        if (result.isError()) {
            QMessageBox::warning(this, tr("Could not add the member"), result.error());
            return;
        }
        refresh();
    });
}

void GroupInfoPanel::makeAdmin()
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedMemberId();
    if (userId.isEmpty()) {
        return;
    }
    AdminRightsDialog dialog(this);
    dialog.setPermissions(group_permissions::EditInfo | group_permissions::Invite
                          | group_permissions::Pin);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_service->setRole(m_group.groupId, userId, GroupRole::Admin, dialog.permissions(),
                       [this](Result<GroupMember> result) {
                           if (result.isError()) {
                               QMessageBox::warning(this, tr("Could not change the role"),
                                                    result.error());
                               return;
                           }
                           refresh();
                       });
}

void GroupInfoPanel::makeMember()
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedMemberId();
    if (userId.isEmpty()) {
        return;
    }
    m_service->setRole(m_group.groupId, userId, GroupRole::Member, 0,
                       [this](Result<GroupMember> result) {
                           if (result.isError()) {
                               QMessageBox::warning(this, tr("Could not change the role"),
                                                    result.error());
                               return;
                           }
                           refresh();
                       });
}

void GroupInfoPanel::removeSelected()
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedMemberId();
    if (userId.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Remove member"),
                              tr("Remove %1 from the group?").arg(selectedMemberLogin()))
        != QMessageBox::Yes) {
        return;
    }
    m_service->removeMember(m_group.groupId, userId, false,
                            [this](Result<bool> result) {
                                if (result.isError()) {
                                    QMessageBox::warning(this, tr("Could not remove the member"),
                                                         result.error());
                                    return;
                                }
                                refresh();
                            });
}

void GroupInfoPanel::banSelected()
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedMemberId();
    if (userId.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Ban member"),
                              tr("Ban %1 from the group?").arg(selectedMemberLogin()))
        != QMessageBox::Yes) {
        return;
    }
    m_service->banMember(m_group.groupId, userId, true,
                         [this](Result<bool> result) {
                             if (result.isError()) {
                                 QMessageBox::warning(this, tr("Could not ban the member"),
                                                      result.error());
                                 return;
                             }
                             refresh();
                         });
}

void GroupInfoPanel::transferSelected()
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedMemberId();
    if (userId.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Transfer ownership"),
                              tr("Make %1 the owner? You will become an admin.")
                                  .arg(selectedMemberLogin()))
        != QMessageBox::Yes) {
        return;
    }
    m_service->transferOwnership(m_group.groupId, userId, [this](Result<Group> result) {
        if (result.isError()) {
            QMessageBox::warning(this, tr("Could not transfer ownership"), result.error());
            return;
        }
        setGroup(result.value());
        emit groupChanged(result.value().groupId);
    });
}

void GroupInfoPanel::approveSelected(bool approve)
{
    if (!m_service) {
        return;
    }
    const QString userId = selectedRequestId();
    if (userId.isEmpty()) {
        return;
    }
    m_service->approveRequest(m_group.groupId, userId, approve,
                              [this](Result<bool> result) {
                                  if (result.isError()) {
                                      QMessageBox::warning(this, tr("Could not update the request"),
                                                           result.error());
                                      return;
                                  }
                                  refresh();
                              });
}

void GroupInfoPanel::createInvite()
{
    if (!m_service) {
        return;
    }
    m_service->createInvite(m_group.groupId, [this](Result<QString> result) {
        if (result.isError()) {
            QMessageBox::warning(this, tr("Could not create an invite"), result.error());
            return;
        }
        m_invite->setText(result.value());
        if (QClipboard *clipboard = QApplication::clipboard()) {
            clipboard->setText(result.value());
        }
    });
}

void GroupInfoPanel::leaveGroup()
{
    if (!m_service) {
        return;
    }
    if (QMessageBox::question(this, tr("Leave group"), tr("Leave this group?"))
        != QMessageBox::Yes) {
        return;
    }
    const QString groupId = m_group.groupId;
    m_service->leaveGroup(groupId, [this, groupId](Result<bool> result) {
        if (result.isError()) {
            QMessageBox::warning(this, tr("Could not leave the group"), result.error());
            return;
        }
        emit groupLeft(groupId);
        accept();
    });
}

} // namespace mini
