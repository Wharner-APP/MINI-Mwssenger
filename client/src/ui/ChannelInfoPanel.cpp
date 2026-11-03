#include "ui/ChannelInfoPanel.h"

#include "services/ChannelService.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {
namespace {

constexpr int kUserIdRole = Qt::UserRole;
constexpr int kLoginRole = Qt::UserRole + 1;

QString roleLabel(ChannelRole role)
{
    switch (role) {
    case ChannelRole::Owner:
        return QObject::tr("Owner");
    case ChannelRole::Admin:
        return QObject::tr("Admin");
    case ChannelRole::Subscriber:
        break;
    }
    return QObject::tr("Subscriber");
}

} // namespace

ChannelInfoPanel::ChannelInfoPanel(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("channelInfoPanel"));
    setWindowTitle(tr("Channel information"));
    setModal(false);
    resize(420, 560);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("channelInfoTitle"));
    m_title->setProperty("miniRole", QStringLiteral("title"));
    m_title->setWordWrap(true);

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("channelInfoCount"));
    m_count->setProperty("miniRole", QStringLiteral("caption"));

    m_about = new QLabel(this);
    m_about->setObjectName(QStringLiteral("channelInfoAbout"));
    m_about->setWordWrap(true);

    m_invite = new QLineEdit(this);
    m_invite->setObjectName(QStringLiteral("channelInviteField"));
    m_invite->setReadOnly(true);
    m_invite->setPlaceholderText(tr("Invite link"));
    m_inviteButton = new QPushButton(tr("Invite"), this);
    m_inviteButton->setObjectName(QStringLiteral("channelInviteButton"));

    m_members = new QListWidget(this);
    m_members->setObjectName(QStringLiteral("channelMemberList"));
    m_members->setMinimumHeight(150);

    m_adminButton = new QPushButton(tr("Make admin"), this);
    m_adminButton->setObjectName(QStringLiteral("channelMakeAdminButton"));
    m_subscriberButton = new QPushButton(tr("Make subscriber"), this);
    m_subscriberButton->setObjectName(QStringLiteral("channelMakeSubscriberButton"));
    m_leaveButton = new QPushButton(tr("Unsubscribe"), this);
    m_leaveButton->setObjectName(QStringLiteral("channelUnsubscribeButton"));
    m_deleteButton = new QPushButton(tr("Delete channel"), this);
    m_deleteButton->setObjectName(QStringLiteral("channelDeleteButton"));

    auto *inviteRow = new QHBoxLayout();
    inviteRow->addWidget(m_invite, 1);
    inviteRow->addWidget(m_inviteButton);

    auto *actions = new QHBoxLayout();
    actions->addWidget(m_adminButton);
    actions->addWidget(m_subscriberButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    layout->addWidget(m_title);
    layout->addWidget(m_count);
    layout->addWidget(m_about);
    layout->addLayout(inviteRow);
    layout->addWidget(new QLabel(tr("Subscribers"), this));
    layout->addWidget(m_members, 1);
    layout->addLayout(actions);
    layout->addWidget(m_leaveButton);
    layout->addWidget(m_deleteButton);
    layout->addWidget(buttons);

    connect(m_inviteButton, &QPushButton::clicked, this, &ChannelInfoPanel::createInvite);
    connect(m_invite, &QLineEdit::cursorPositionChanged, this, &ChannelInfoPanel::copyInvite);
    connect(m_adminButton, &QPushButton::clicked, this, &ChannelInfoPanel::makeAdmin);
    connect(m_subscriberButton, &QPushButton::clicked, this,
            &ChannelInfoPanel::makeSubscriber);
    connect(m_leaveButton, &QPushButton::clicked, this, &ChannelInfoPanel::unsubscribe);
    connect(m_deleteButton, &QPushButton::clicked, this, &ChannelInfoPanel::deleteChannel);
    connect(m_members, &QListWidget::currentRowChanged, this,
            [this](int) { updatePermissions(); });

    updatePermissions();
}

void ChannelInfoPanel::setChannelService(services::ChannelService *service)
{
    m_service = service;
}

void ChannelInfoPanel::setChannel(const Channel &channel)
{
    m_channel = channel;
    rebuild();
}

void ChannelInfoPanel::refresh()
{
    if (!m_service || m_channel.channelId.isEmpty()) {
        return;
    }
    m_service->loadChannel(m_channel.channelId, [this](Result<Channel> result) {
        if (result.isOk()) {
            setChannel(result.value());
        }
    });
}

void ChannelInfoPanel::rebuild()
{
    m_title->setText(m_channel.title);
    m_about->setText(m_channel.about);
    m_count->setText(tr("%n subscriber(s)", "", m_channel.subscriberCount));
    m_invite->setText(m_channel.inviteLink);
    m_members->clear();
    for (const ChannelMember &member : m_channel.members) {
        const QString name = member.displayName.isEmpty() ? member.login
                                                           : member.displayName;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(name, roleLabel(member.role)), m_members);
        item->setData(kUserIdRole, member.userId);
        item->setData(kLoginRole, member.login);
    }
    updatePermissions();
}

void ChannelInfoPanel::updatePermissions()
{
    const bool canInvite = m_channel.can(channel_permissions::Invite);
    const bool canAssign = m_channel.can(channel_permissions::AssignAdmins);
    const bool selected = !selectedMemberId().isEmpty();
    m_inviteButton->setEnabled(canInvite);
    m_adminButton->setEnabled(canAssign && selected);
    m_subscriberButton->setEnabled(canAssign && selected);
    m_leaveButton->setEnabled(m_channel.isMember && !m_channel.isOwner());
    m_deleteButton->setEnabled(m_channel.isOwner());
}

QString ChannelInfoPanel::selectedMemberId() const
{
    const QListWidgetItem *item = m_members->currentItem();
    if (!item) {
        return {};
    }
    return item->data(kUserIdRole).toString();
}

void ChannelInfoPanel::createInvite()
{
    if (!m_service || m_channel.channelId.isEmpty()) {
        return;
    }
    m_service->createInvite(m_channel.channelId, [this](Result<QString> result) {
        if (result.isOk()) {
            m_channel.inviteLink = result.value();
            m_invite->setText(result.value());
            emit channelChanged(m_channel.channelId);
        }
    });
}

void ChannelInfoPanel::copyInvite()
{
    if (!m_invite->text().isEmpty() && QApplication::clipboard()) {
        QApplication::clipboard()->setText(m_invite->text());
    }
}

void ChannelInfoPanel::makeAdmin()
{
    const QString userId = selectedMemberId();
    if (!m_service || userId.isEmpty()) {
        return;
    }
    m_service->setRole(m_channel.channelId, userId, ChannelRole::Admin,
                       channel_permissions::Publish | channel_permissions::Invite,
                       [this](Result<ChannelMember>) {
                           refresh();
                           emit channelChanged(m_channel.channelId);
                       });
}

void ChannelInfoPanel::makeSubscriber()
{
    const QString userId = selectedMemberId();
    if (!m_service || userId.isEmpty()) {
        return;
    }
    m_service->setRole(m_channel.channelId, userId, ChannelRole::Subscriber, 0,
                       [this](Result<ChannelMember>) {
                           refresh();
                           emit channelChanged(m_channel.channelId);
                       });
}

void ChannelInfoPanel::unsubscribe()
{
    if (!m_service || m_channel.channelId.isEmpty()) {
        return;
    }
    const QString channelId = m_channel.channelId;
    m_service->unsubscribe(channelId, [this, channelId](Result<bool> result) {
        if (result.isOk()) {
            emit channelLeft(channelId);
            close();
        }
    });
}

void ChannelInfoPanel::deleteChannel()
{
    if (!m_service || m_channel.channelId.isEmpty()) {
        return;
    }
    const QString channelId = m_channel.channelId;
    m_service->deleteChannel(channelId, [this, channelId](Result<bool> result) {
        if (result.isOk()) {
            emit channelDeleted(channelId);
            close();
        }
    });
}

} // namespace mini
