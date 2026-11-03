#pragma once

#include "domain/Group.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace mini {

namespace services {
class GroupService;
}

// S19 GroupInfo: avatar/name/about, member list, invite link, admin actions
// and join requests. Reads and writes through GroupService only.
class GroupInfoPanel : public QDialog
{
    Q_OBJECT

public:
    explicit GroupInfoPanel(QWidget *parent = nullptr);

    void setGroupService(services::GroupService *service);
    void setGroup(const Group &group);
    [[nodiscard]] Group group() const { return m_group; }

    // Reloads the roster from the server when a service is attached.
    void refresh();

    [[nodiscard]] QLabel *titleLabel() const { return m_title; }
    [[nodiscard]] QLabel *aboutLabel() const { return m_about; }
    [[nodiscard]] QLabel *memberCountLabel() const { return m_memberCount; }
    [[nodiscard]] QListWidget *memberList() const { return m_members; }
    [[nodiscard]] QListWidget *requestList() const { return m_requests; }
    [[nodiscard]] QLineEdit *inviteField() const { return m_invite; }
    [[nodiscard]] QPushButton *inviteButton() const { return m_inviteButton; }
    [[nodiscard]] QPushButton *addMemberButton() const { return m_addButton; }
    [[nodiscard]] QPushButton *makeAdminButton() const { return m_adminButton; }
    [[nodiscard]] QPushButton *removeButton() const { return m_removeButton; }
    [[nodiscard]] QPushButton *banButton() const { return m_banButton; }
    [[nodiscard]] QPushButton *transferButton() const { return m_transferButton; }
    [[nodiscard]] QPushButton *approveButton() const { return m_approveButton; }
    [[nodiscard]] QPushButton *rejectButton() const { return m_rejectButton; }
    [[nodiscard]] QPushButton *leaveButton() const { return m_leaveButton; }

signals:
    void groupLeft(const QString &groupId);
    void groupChanged(const QString &groupId);

private:
    void rebuild();
    void updatePermissions();
    [[nodiscard]] QString selectedMemberId() const;
    [[nodiscard]] QString selectedMemberLogin() const;
    [[nodiscard]] QString selectedRequestId() const;
    [[nodiscard]] GroupRole selectedMemberRole() const;

    void addMember();
    void makeAdmin();
    void makeMember();
    void removeSelected();
    void banSelected();
    void transferSelected();
    void approveSelected(bool approve);
    void createInvite();
    void leaveGroup();

    services::GroupService *m_service = nullptr;
    Group m_group;

    QLabel *m_title = nullptr;
    QLabel *m_about = nullptr;
    QLabel *m_memberCount = nullptr;
    QLineEdit *m_invite = nullptr;
    QListWidget *m_members = nullptr;
    QListWidget *m_requests = nullptr;
    QPushButton *m_inviteButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_adminButton = nullptr;
    QPushButton *m_memberButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_banButton = nullptr;
    QPushButton *m_transferButton = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_leaveButton = nullptr;
};

} // namespace mini
