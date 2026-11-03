#pragma once

#include <QString>
#include <QVector>

namespace mini {

// Channel roles (§11.7, §29.9). The numeric value is the rank used for
// comparisons (owner > admin > subscriber).
enum class ChannelRole {
    Subscriber = 0,
    Admin = 1,
    Owner = 2,
};

[[nodiscard]] QString channelRoleToString(ChannelRole role);
[[nodiscard]] ChannelRole channelRoleFromString(const QString &value);

// 12-bit channel permission mask, same layout as the server
// (server/app/services/channel_service.py). The owner holds every bit.
namespace channel_permissions {
constexpr quint32 EditInfo = 1u << 0;
constexpr quint32 Publish = 1u << 1;
constexpr quint32 DeletePosts = 1u << 2;
constexpr quint32 Invite = 1u << 3;
constexpr quint32 Pin = 1u << 4;
constexpr quint32 Voice = 1u << 5;
constexpr quint32 AssignAdmins = 1u << 6;
constexpr quint32 SlowMode = 1u << 7;
constexpr quint32 MentionAll = 1u << 8;
constexpr quint32 Stories = 1u << 9;
constexpr quint32 Approve = 1u << 10;
constexpr quint32 Description = 1u << 11;
constexpr quint32 All = (1u << 12) - 1;
} // namespace channel_permissions

struct ChannelMember
{
    QString userId;
    QString login;
    QString displayName;
    ChannelRole role = ChannelRole::Subscriber;
    quint32 permissions = 0;
    qint64 joinedAt = 0;

    [[nodiscard]] bool isOwner() const { return role == ChannelRole::Owner; }
    [[nodiscard]] bool isAdmin() const { return role == ChannelRole::Admin; }
    [[nodiscard]] bool can(quint32 bit) const
    {
        return role == ChannelRole::Owner || (permissions & bit) != 0;
    }
};

struct Channel
{
    QString channelId;
    QString title;
    QString about;
    QString avatarMedia;
    QString ownerId;
    bool isPublic = false;
    QString linkedGroup;
    QString inviteToken;
    QString inviteLink;
    // Empty `my_role` on the wire means "not a member" (public preview).
    ChannelRole myRole = ChannelRole::Subscriber;
    bool isMember = false;
    quint32 myPermissions = 0;
    int subscriberCount = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVector<ChannelMember> members;

    [[nodiscard]] bool isValid() const { return !channelId.isEmpty(); }
    [[nodiscard]] bool can(quint32 bit) const
    {
        return isMember && (myRole == ChannelRole::Owner || (myPermissions & bit) != 0);
    }
    [[nodiscard]] bool isOwner() const { return isMember && myRole == ChannelRole::Owner; }
    // §43.2: only admins with the publish bit (or the owner) may post.
    [[nodiscard]] bool canPublish() const { return can(channel_permissions::Publish); }
};

namespace channel_ids {
// Local chat row key. Prefix keeps channel chats distinguishable from direct
// and group chats without another column.
inline constexpr auto kChatPrefix = "channel_";

[[nodiscard]] inline QString chatId(const QString &channelId)
{
    return QStringLiteral("channel_") + channelId;
}

[[nodiscard]] inline bool isChannelChat(const QString &chatId)
{
    return chatId.startsWith(QLatin1String(kChatPrefix));
}

[[nodiscard]] inline QString channelIdFromChat(const QString &chatId)
{
    return isChannelChat(chatId) ? chatId.mid(8) : QString();
}
} // namespace channel_ids

} // namespace mini
