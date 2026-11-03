#pragma once

#include "domain/Channel.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace mini {

namespace services {
class ChannelService;
}

// S20 ChannelInfo: name/about, subscriber count, roster, invite link and the
// admin actions that the current role allows (§33.11, §43).
class ChannelInfoPanel : public QDialog
{
    Q_OBJECT

public:
    explicit ChannelInfoPanel(QWidget *parent = nullptr);

    void setChannelService(services::ChannelService *service);
    void setChannel(const Channel &channel);
    [[nodiscard]] Channel channel() const { return m_channel; }

    // Reloads metadata and roster from the server when a service is attached.
    void refresh();

    [[nodiscard]] QLabel *titleLabel() const { return m_title; }
    [[nodiscard]] QLabel *aboutLabel() const { return m_about; }
    [[nodiscard]] QLabel *subscriberCountLabel() const { return m_count; }
    [[nodiscard]] QListWidget *memberList() const { return m_members; }
    [[nodiscard]] QLineEdit *inviteField() const { return m_invite; }
    [[nodiscard]] QPushButton *inviteButton() const { return m_inviteButton; }
    [[nodiscard]] QPushButton *makeAdminButton() const { return m_adminButton; }
    [[nodiscard]] QPushButton *makeSubscriberButton() const { return m_subscriberButton; }
    [[nodiscard]] QPushButton *unsubscribeButton() const { return m_leaveButton; }
    [[nodiscard]] QPushButton *deleteButton() const { return m_deleteButton; }

signals:
    void channelLeft(const QString &channelId);
    void channelDeleted(const QString &channelId);
    void channelChanged(const QString &channelId);

private:
    void rebuild();
    void updatePermissions();
    [[nodiscard]] QString selectedMemberId() const;
    void createInvite();
    void makeAdmin();
    void makeSubscriber();
    void unsubscribe();
    void deleteChannel();
    void copyInvite();

    services::ChannelService *m_service = nullptr;
    Channel m_channel;

    QLabel *m_title = nullptr;
    QLabel *m_about = nullptr;
    QLabel *m_count = nullptr;
    QLineEdit *m_invite = nullptr;
    QListWidget *m_members = nullptr;
    QPushButton *m_inviteButton = nullptr;
    QPushButton *m_adminButton = nullptr;
    QPushButton *m_subscriberButton = nullptr;
    QPushButton *m_leaveButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
};

} // namespace mini
