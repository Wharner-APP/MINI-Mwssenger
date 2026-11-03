#include "storage/GroupsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

namespace mini::storage {
namespace {

constexpr auto kGroupColumns =
    "group_id, title, about, avatar_media, owner_id, is_public, slow_mode_sec, "
    "join_by_request, invite_token, invite_link, my_role, my_permissions, "
    "member_count, created_at, updated_at";

Group readGroup(const detail::Statement &statement)
{
    Group group;
    group.groupId = statement.text(0);
    group.title = statement.text(1);
    group.about = statement.text(2);
    group.avatarMedia = statement.text(3);
    group.ownerId = statement.text(4);
    group.isPublic = statement.boolean(5);
    group.slowModeSec = statement.integer(6);
    group.joinByRequest = statement.boolean(7);
    group.inviteToken = statement.text(8);
    group.inviteLink = statement.text(9);
    group.myRole = groupRoleFromString(statement.text(10));
    group.myPermissions = static_cast<quint32>(statement.int64(11));
    group.memberCount = statement.integer(12);
    group.createdAt = statement.int64(13);
    group.updatedAt = statement.int64(14);
    return group;
}

GroupMember readMember(const detail::Statement &statement)
{
    GroupMember member;
    member.userId = statement.text(0);
    member.login = statement.text(1);
    member.displayName = statement.text(2);
    member.role = groupRoleFromString(statement.text(3));
    member.permissions = static_cast<quint32>(statement.int64(4));
    member.joinedAt = statement.int64(5);
    member.mutedUntil = statement.int64(6);
    return member;
}

QByteArray openText(const BodyCipher &cipher, const QByteArray &stored, const QByteArray &aad)
{
    if (stored.isEmpty()) {
        return {};
    }
    const auto plain = cipher.decrypt(stored, aad);
    if (plain.isError()) {
        return {};
    }
    return QByteArray::fromBase64(plain.value().toLatin1());
}

} // namespace

QByteArray groupSenderChainAad(const QString &groupId, const QString &senderId)
{
    return QByteArrayLiteral("mini/group-chain/v1:") + groupId.toUtf8() + ':'
        + senderId.toUtf8();
}

QByteArray groupSenderSigningAad(const QString &groupId, const QString &senderId)
{
    return QByteArrayLiteral("mini/group-signing/v1:") + groupId.toUtf8() + ':'
        + senderId.toUtf8();
}

GroupsRepo::GroupsRepo(Database &db) : m_db(db), m_cipher(db.dbKey())
{
}

Result<QVector<Group>> GroupsRepo::listGroups() const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM groups ORDER BY updated_at DESC, group_id ASC")
            .arg(QString::fromLatin1(kGroupColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Group>>::error(detail::sqlError(m_db.handle(), "groups.list"));
    }
    QVector<Group> groups;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        groups.push_back(readGroup(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Group>>::error(detail::sqlError(m_db.handle(), "groups.list"));
    }
    return Result<QVector<Group>>::ok(groups);
}

Result<std::optional<Group>> GroupsRepo::getGroup(const QString &groupId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM groups WHERE group_id = ?")
            .arg(QString::fromLatin1(kGroupColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Group>>::error(
            detail::sqlError(m_db.handle(), "groups.get"));
    }
    statement.bindText(1, groupId);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Group>>::ok(readGroup(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Group>>::error(
            detail::sqlError(m_db.handle(), "groups.get"));
    }
    return Result<std::optional<Group>>::ok(std::nullopt);
}

Result<bool> GroupsRepo::upsertGroup(const Group &group)
{
    if (group.groupId.isEmpty()) {
        return Result<bool>::error(QStringLiteral("group_id is required"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO groups(group_id, title, about, avatar_media, owner_id, is_public,
                                slow_mode_sec, join_by_request, invite_token, invite_link,
                                my_role, my_permissions, member_count, created_at, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(group_id) DO UPDATE SET
                  title = excluded.title,
                  about = excluded.about,
                  avatar_media = excluded.avatar_media,
                  owner_id = excluded.owner_id,
                  is_public = excluded.is_public,
                  slow_mode_sec = excluded.slow_mode_sec,
                  join_by_request = excluded.join_by_request,
                  invite_token = CASE WHEN excluded.invite_token <> ''
                                      THEN excluded.invite_token ELSE groups.invite_token END,
                  invite_link = CASE WHEN excluded.invite_link <> ''
                                     THEN excluded.invite_link ELSE groups.invite_link END,
                  my_role = excluded.my_role,
                  my_permissions = excluded.my_permissions,
                  member_count = excluded.member_count,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.upsert"));
    }
    statement.bindText(1, group.groupId);
    statement.bindText(2, group.title);
    statement.bindText(3, group.about);
    statement.bindText(4, group.avatarMedia);
    statement.bindText(5, group.ownerId);
    statement.bindBool(6, group.isPublic);
    statement.bindInt(7, group.slowModeSec);
    statement.bindBool(8, group.joinByRequest);
    statement.bindText(9, group.inviteToken);
    statement.bindText(10, group.inviteLink);
    statement.bindText(11, groupRoleToString(group.myRole));
    statement.bindInt64(12, static_cast<qint64>(group.myPermissions));
    statement.bindInt(13, group.memberCount);
    statement.bindInt64(14, group.createdAt);
    statement.bindInt64(15, group.updatedAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<bool> GroupsRepo::removeGroup(const QString &groupId)
{
    return m_db.transaction([&](Database &tx) {
        const QVector<QByteArray> statements{
            QByteArrayLiteral("DELETE FROM group_members WHERE group_id = ?"),
            QByteArrayLiteral("DELETE FROM group_sender_keys WHERE group_id = ?"),
            QByteArrayLiteral("DELETE FROM groups WHERE group_id = ?"),
        };
        for (const QByteArray &sql : statements) {
            detail::Statement statement(tx.handle(), sql.constData());
            if (!statement.isOk()) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "groups.remove"));
            }
            statement.bindText(1, groupId);
            if (statement.step() != SQLITE_DONE) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "groups.remove"));
            }
        }
        return Result<bool>::ok(true);
    });
}

Result<QVector<GroupMember>> GroupsRepo::members(const QString &groupId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT user_id, login, display_name, role, permissions, joined_at, muted_until"
        " FROM group_members WHERE group_id = ?"
        " ORDER BY CASE role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END,"
        " joined_at, login");
    if (!statement.isOk()) {
        return Result<QVector<GroupMember>>::error(
            detail::sqlError(m_db.handle(), "groups.members"));
    }
    statement.bindText(1, groupId);
    QVector<GroupMember> members;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        members.push_back(readMember(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<GroupMember>>::error(
            detail::sqlError(m_db.handle(), "groups.members"));
    }
    return Result<QVector<GroupMember>>::ok(members);
}

Result<bool> GroupsRepo::replaceMembers(const QString &groupId,
                                        const QVector<GroupMember> &members)
{
    return m_db.transaction([&](Database &tx) {
        {
            detail::Statement clear(tx.handle(),
                                    "DELETE FROM group_members WHERE group_id = ?");
            if (!clear.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "groups.replaceMembers"));
            }
            clear.bindText(1, groupId);
            if (clear.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "groups.replaceMembers"));
            }
        }
        for (const GroupMember &member : members) {
            detail::Statement insert(
                tx.handle(),
                "INSERT INTO group_members(group_id, user_id, login, display_name, role,"
                " permissions, joined_at, muted_until) VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
            if (!insert.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "groups.replaceMembers"));
            }
            insert.bindText(1, groupId);
            insert.bindText(2, member.userId);
            insert.bindText(3, member.login);
            insert.bindText(4, member.displayName);
            insert.bindText(5, groupRoleToString(member.role));
            insert.bindInt64(6, static_cast<qint64>(member.permissions));
            insert.bindInt64(7, member.joinedAt);
            insert.bindInt64(8, member.mutedUntil);
            if (insert.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "groups.replaceMembers"));
            }
        }
        return Result<bool>::ok(true);
    });
}

Result<bool> GroupsRepo::upsertMember(const QString &groupId, const GroupMember &member)
{
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO group_members(group_id, user_id, login, display_name, role,
                                        permissions, joined_at, muted_until)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(group_id, user_id) DO UPDATE SET
                  login = excluded.login,
                  display_name = excluded.display_name,
                  role = excluded.role,
                  permissions = excluded.permissions,
                  muted_until = excluded.muted_until)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.upsertMember"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, member.userId);
    statement.bindText(3, member.login);
    statement.bindText(4, member.displayName);
    statement.bindText(5, groupRoleToString(member.role));
    statement.bindInt64(6, static_cast<qint64>(member.permissions));
    statement.bindInt64(7, member.joinedAt);
    statement.bindInt64(8, member.mutedUntil);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.upsertMember"));
    }
    return Result<bool>::ok(true);
}

Result<bool> GroupsRepo::removeMember(const QString &groupId, const QString &userId)
{
    detail::Statement statement(
        m_db.handle(), "DELETE FROM group_members WHERE group_id = ? AND user_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeMember"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, userId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeMember"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<std::optional<GroupMember>> GroupsRepo::memberByLogin(const QString &groupId,
                                                             const QString &login) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT user_id, login, display_name, role, permissions, joined_at, muted_until"
        " FROM group_members WHERE group_id = ? AND login = ? LIMIT 1");
    if (!statement.isOk()) {
        return Result<std::optional<GroupMember>>::error(
            detail::sqlError(m_db.handle(), "groups.memberByLogin"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, login);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<GroupMember>>::ok(readMember(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<GroupMember>>::error(
            detail::sqlError(m_db.handle(), "groups.memberByLogin"));
    }
    return Result<std::optional<GroupMember>>::ok(std::nullopt);
}

Result<bool> GroupsRepo::upsertSenderKey(const QString &groupId, const QString &senderId,
                                         const crypto::SenderKeyState &state)
{
    const QByteArray chainAad = groupSenderChainAad(groupId, senderId);
    const QByteArray signingAad = groupSenderSigningAad(groupId, senderId);
    const auto sealedChain = m_cipher.encrypt(
        QString::fromLatin1(state.chainKey.toBase64()), chainAad);
    if (sealedChain.isError()) {
        return Result<bool>::error(sealedChain.error());
    }
    QByteArray sealedSigning;
    if (!state.signingPrivate.isEmpty()) {
        const auto sealed = m_cipher.encrypt(
            QString::fromLatin1(state.signingPrivate.toBase64()), signingAad);
        if (sealed.isError()) {
            return Result<bool>::error(sealed.error());
        }
        sealedSigning = sealed.value();
    }

    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO group_sender_keys(group_id, sender_id, key_id, iteration,
                                            prev_iteration, chain_key, signing_public,
                                            signing_private, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(group_id, sender_id) DO UPDATE SET
                  key_id = excluded.key_id,
                  iteration = excluded.iteration,
                  prev_iteration = excluded.prev_iteration,
                  chain_key = excluded.chain_key,
                  signing_public = excluded.signing_public,
                  signing_private = CASE WHEN length(excluded.signing_private) > 0
                                         THEN excluded.signing_private
                                         ELSE group_sender_keys.signing_private END,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.senderKey"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, senderId);
    statement.bindInt64(3, static_cast<qint64>(state.keyId));
    statement.bindInt64(4, static_cast<qint64>(state.iteration));
    statement.bindInt64(5, static_cast<qint64>(state.previousIteration));
    statement.bindBlob(6, sealedChain.value());
    statement.bindBlob(7, state.signingPublic);
    statement.bindBlob(8, sealedSigning);
    statement.bindInt64(9, static_cast<qint64>(state.iteration));
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.senderKey"));
    }
    return Result<bool>::ok(true);
}

Result<std::optional<crypto::SenderKeyState>> GroupsRepo::senderKey(
    const QString &groupId, const QString &senderId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT key_id, iteration, prev_iteration, chain_key, signing_public, signing_private"
        " FROM group_sender_keys WHERE group_id = ? AND sender_id = ?");
    if (!statement.isOk()) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            detail::sqlError(m_db.handle(), "groups.senderKey"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, senderId);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<std::optional<crypto::SenderKeyState>>::ok(std::nullopt);
    }
    if (rc != SQLITE_ROW) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            detail::sqlError(m_db.handle(), "groups.senderKey"));
    }
    crypto::SenderKeyState state;
    state.keyId = static_cast<quint32>(statement.int64(0));
    state.iteration = static_cast<quint32>(statement.int64(1));
    state.previousIteration = static_cast<quint32>(statement.int64(2));
    state.chainKey = openText(m_cipher, statement.blob(3),
                              groupSenderChainAad(groupId, senderId));
    state.signingPublic = statement.blob(4);
    state.signingPrivate = openText(m_cipher, statement.blob(5),
                                    groupSenderSigningAad(groupId, senderId));
    if (!state.isValid()) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            QStringLiteral("sender_key_corrupt"));
    }
    return Result<std::optional<crypto::SenderKeyState>>::ok(state);
}

Result<bool> GroupsRepo::removeSenderKey(const QString &groupId, const QString &senderId)
{
    detail::Statement statement(
        m_db.handle(),
        "DELETE FROM group_sender_keys WHERE group_id = ? AND sender_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeSenderKey"));
    }
    statement.bindText(1, groupId);
    statement.bindText(2, senderId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeSenderKey"));
    }
    return Result<bool>::ok(true);
}

Result<bool> GroupsRepo::removeSenderKeys(const QString &groupId)
{
    detail::Statement statement(m_db.handle(),
                                "DELETE FROM group_sender_keys WHERE group_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeSenderKeys"));
    }
    statement.bindText(1, groupId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "groups.removeSenderKeys"));
    }
    return Result<bool>::ok(true);
}

} // namespace mini::storage
