#pragma once

#include <QString>
#include <QVector>

namespace mini {

// Roles fixed by §42 / server group_service.py. The numeric value is the
// rank used for comparisons (owner > admin > member).
enum class GroupRole {
    Member = 0,
    Admin = 1,
    Owner = 2,
};

[[nodiscard]] QString groupRoleToString(GroupRole role);
[[nodiscard]] GroupRole groupRoleFromString(const QString &value);

// 12-bit admin permission mask shared with the server (§30.4.7). The owner
// implicitly holds every bit.
namespace group_permissions {
constexpr quint32 EditInfo = 1u << 0;
constexpr quint32 DeleteMessages = 1u << 1;
constexpr quint32 Ban = 1u << 2;
constexpr quint32 Invite = 1u << 3;
constexpr quint32 Pin = 1u << 4;
constexpr quint32 Voice = 1u << 5;
constexpr quint32 AssignAdmins = 1u << 6;
constexpr quint32 SlowMode = 1u << 7;
constexpr quint32 MentionAll = 1u << 8;
constexpr quint32 Stories = 1u << 9;
constexpr quint32 Approve = 1u << 10;
constexpr quint32 EditDescription = 1u << 11;
constexpr quint32 All = (1u << 12) - 1;
} // namespace group_permissions

struct GroupMember
{
    QString userId;
    QString login;
    QString displayName;
    GroupRole role = GroupRole::Member;
    quint32 permissions = 0;
    qint64 joinedAt = 0;
    qint64 mutedUntil = 0;

    [[nodiscard]] bool isOwner() const { return role == GroupRole::Owner; }
    [[nodiscard]] bool isAdmin() const { return role == GroupRole::Admin; }
    [[nodiscard]] bool can(quint32 bit) const
    {
        return role == GroupRole::Owner || (permissions & bit) != 0;
    }
};

struct Group
{
    QString groupId;
    QString title;
    QString about;
    QString avatarMedia;
    QString ownerId;
    bool isPublic = false;
    int slowModeSec = 0;
    bool joinByRequest = false;
    QString inviteToken;
    QString inviteLink;
    GroupRole myRole = GroupRole::Member;
    quint32 myPermissions = 0;
    int memberCount = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVector<GroupMember> members;

    [[nodiscard]] bool isValid() const { return !groupId.isEmpty(); }
    [[nodiscard]] bool can(quint32 bit) const
    {
        return myRole == GroupRole::Owner || (myPermissions & bit) != 0;
    }
    [[nodiscard]] bool isOwner() const { return myRole == GroupRole::Owner; }
};

struct GroupJoinRequest
{
    QString groupId;
    QString userId;
    QString login;
    QString displayName;
    QString message;
    QString status = QStringLiteral("pending");
    qint64 createdAt = 0;
};

namespace group_ids {
// Local chat row key. Prefix keeps group chats distinguishable from the
// hashed direct-chat ids without another column.
inline constexpr auto kChatPrefix = "group_";

[[nodiscard]] inline QString chatId(const QString &groupId)
{
    return QStringLiteral("group_") + groupId;
}

[[nodiscard]] inline bool isGroupChat(const QString &chatId)
{
    return chatId.startsWith(QLatin1String(kChatPrefix));
}

[[nodiscard]] inline QString groupIdFromChat(const QString &chatId)
{
    return isGroupChat(chatId) ? chatId.mid(6) : QString();
}
} // namespace group_ids

} // namespace mini
