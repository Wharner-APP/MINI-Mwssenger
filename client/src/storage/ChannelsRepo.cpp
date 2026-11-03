#include "storage/ChannelsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

namespace mini::storage {
namespace {

constexpr auto kChannelColumns =
    "channel_id, title, about, avatar_media, owner_id, is_public, linked_group, "
    "invite_token, invite_link, my_role, is_member, my_permissions, "
    "subscriber_count, created_at, updated_at";

Channel readChannel(const detail::Statement &statement)
{
    Channel channel;
    channel.channelId = statement.text(0);
    channel.title = statement.text(1);
    channel.about = statement.text(2);
    channel.avatarMedia = statement.text(3);
    channel.ownerId = statement.text(4);
    channel.isPublic = statement.boolean(5);
    channel.linkedGroup = statement.text(6);
    channel.inviteToken = statement.text(7);
    channel.inviteLink = statement.text(8);
    channel.myRole = channelRoleFromString(statement.text(9));
    channel.isMember = statement.boolean(10);
    channel.myPermissions = static_cast<quint32>(statement.int64(11));
    channel.subscriberCount = statement.integer(12);
    channel.createdAt = statement.int64(13);
    channel.updatedAt = statement.int64(14);
    return channel;
}

ChannelMember readMember(const detail::Statement &statement)
{
    ChannelMember member;
    member.userId = statement.text(0);
    member.login = statement.text(1);
    member.displayName = statement.text(2);
    member.role = channelRoleFromString(statement.text(3));
    member.permissions = static_cast<quint32>(statement.int64(4));
    member.joinedAt = statement.int64(5);
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

QByteArray channelSenderChainAad(const QString &channelId, const QString &senderId)
{
    return QByteArrayLiteral("mini/channel-chain/v1:") + channelId.toUtf8() + ':'
        + senderId.toUtf8();
}

QByteArray channelSenderSigningAad(const QString &channelId, const QString &senderId)
{
    return QByteArrayLiteral("mini/channel-signing/v1:") + channelId.toUtf8() + ':'
        + senderId.toUtf8();
}

ChannelsRepo::ChannelsRepo(Database &db) : m_db(db), m_cipher(db.dbKey())
{
}

Result<QVector<Channel>> ChannelsRepo::listChannels() const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM channels ORDER BY updated_at DESC, channel_id ASC")
            .arg(QString::fromLatin1(kChannelColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Channel>>::error(
            detail::sqlError(m_db.handle(), "channels.list"));
    }
    QVector<Channel> channels;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        channels.push_back(readChannel(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Channel>>::error(
            detail::sqlError(m_db.handle(), "channels.list"));
    }
    return Result<QVector<Channel>>::ok(channels);
}

Result<std::optional<Channel>> ChannelsRepo::getChannel(const QString &channelId) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM channels WHERE channel_id = ?")
            .arg(QString::fromLatin1(kChannelColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Channel>>::error(
            detail::sqlError(m_db.handle(), "channels.get"));
    }
    statement.bindText(1, channelId);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Channel>>::ok(readChannel(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Channel>>::error(
            detail::sqlError(m_db.handle(), "channels.get"));
    }
    return Result<std::optional<Channel>>::ok(std::nullopt);
}

Result<bool> ChannelsRepo::upsertChannel(const Channel &channel)
{
    if (channel.channelId.isEmpty()) {
        return Result<bool>::error(QStringLiteral("channel_id is required"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO channels(channel_id, title, about, avatar_media, owner_id, is_public,
                                   linked_group, invite_token, invite_link, my_role, is_member,
                                   my_permissions, subscriber_count, created_at, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(channel_id) DO UPDATE SET
                  title = excluded.title,
                  about = excluded.about,
                  avatar_media = excluded.avatar_media,
                  owner_id = excluded.owner_id,
                  is_public = excluded.is_public,
                  linked_group = excluded.linked_group,
                  invite_token = CASE WHEN excluded.invite_token <> ''
                                      THEN excluded.invite_token ELSE channels.invite_token END,
                  invite_link = CASE WHEN excluded.invite_link <> ''
                                     THEN excluded.invite_link ELSE channels.invite_link END,
                  my_role = excluded.my_role,
                  is_member = excluded.is_member,
                  my_permissions = excluded.my_permissions,
                  subscriber_count = excluded.subscriber_count,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.upsert"));
    }
    statement.bindText(1, channel.channelId);
    statement.bindText(2, channel.title);
    statement.bindText(3, channel.about);
    statement.bindText(4, channel.avatarMedia);
    statement.bindText(5, channel.ownerId);
    statement.bindBool(6, channel.isPublic);
    statement.bindText(7, channel.linkedGroup);
    statement.bindText(8, channel.inviteToken);
    statement.bindText(9, channel.inviteLink);
    statement.bindText(10, channelRoleToString(channel.myRole));
    statement.bindBool(11, channel.isMember);
    statement.bindInt64(12, static_cast<qint64>(channel.myPermissions));
    statement.bindInt(13, channel.subscriberCount);
    statement.bindInt64(14, channel.createdAt);
    statement.bindInt64(15, channel.updatedAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<bool> ChannelsRepo::removeChannel(const QString &channelId)
{
    return m_db.transaction([&](Database &tx) {
        const QVector<QByteArray> statements{
            QByteArrayLiteral("DELETE FROM channel_members WHERE channel_id = ?"),
            QByteArrayLiteral("DELETE FROM channel_sender_keys WHERE channel_id = ?"),
            QByteArrayLiteral("DELETE FROM channels WHERE channel_id = ?"),
        };
        for (const QByteArray &sql : statements) {
            detail::Statement statement(tx.handle(), sql.constData());
            if (!statement.isOk()) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "channels.remove"));
            }
            statement.bindText(1, channelId);
            if (statement.step() != SQLITE_DONE) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "channels.remove"));
            }
        }
        return Result<bool>::ok(true);
    });
}

Result<QVector<ChannelMember>> ChannelsRepo::members(const QString &channelId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT user_id, login, display_name, role, permissions, joined_at"
        " FROM channel_members WHERE channel_id = ?"
        " ORDER BY CASE role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END,"
        " joined_at, login");
    if (!statement.isOk()) {
        return Result<QVector<ChannelMember>>::error(
            detail::sqlError(m_db.handle(), "channels.members"));
    }
    statement.bindText(1, channelId);
    QVector<ChannelMember> members;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        members.push_back(readMember(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<ChannelMember>>::error(
            detail::sqlError(m_db.handle(), "channels.members"));
    }
    return Result<QVector<ChannelMember>>::ok(members);
}

Result<bool> ChannelsRepo::replaceMembers(const QString &channelId,
                                          const QVector<ChannelMember> &members)
{
    return m_db.transaction([&](Database &tx) {
        {
            detail::Statement clear(tx.handle(),
                                    "DELETE FROM channel_members WHERE channel_id = ?");
            if (!clear.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "channels.replaceMembers"));
            }
            clear.bindText(1, channelId);
            if (clear.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "channels.replaceMembers"));
            }
        }
        for (const ChannelMember &member : members) {
            detail::Statement insert(
                tx.handle(),
                "INSERT INTO channel_members(channel_id, user_id, login, display_name, role,"
                " permissions, joined_at) VALUES(?, ?, ?, ?, ?, ?, ?)");
            if (!insert.isOk()) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "channels.replaceMembers"));
            }
            insert.bindText(1, channelId);
            insert.bindText(2, member.userId);
            insert.bindText(3, member.login);
            insert.bindText(4, member.displayName);
            insert.bindText(5, channelRoleToString(member.role));
            insert.bindInt64(6, static_cast<qint64>(member.permissions));
            insert.bindInt64(7, member.joinedAt);
            if (insert.step() != SQLITE_DONE) {
                return Result<bool>::error(
                    detail::sqlError(tx.handle(), "channels.replaceMembers"));
            }
        }
        return Result<bool>::ok(true);
    });
}

Result<bool> ChannelsRepo::upsertMember(const QString &channelId, const ChannelMember &member)
{
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO channel_members(channel_id, user_id, login, display_name, role,
                                          permissions, joined_at)
              VALUES(?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(channel_id, user_id) DO UPDATE SET
                  login = excluded.login,
                  display_name = excluded.display_name,
                  role = excluded.role,
                  permissions = excluded.permissions)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.upsertMember"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, member.userId);
    statement.bindText(3, member.login);
    statement.bindText(4, member.displayName);
    statement.bindText(5, channelRoleToString(member.role));
    statement.bindInt64(6, static_cast<qint64>(member.permissions));
    statement.bindInt64(7, member.joinedAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.upsertMember"));
    }
    return Result<bool>::ok(true);
}

Result<bool> ChannelsRepo::removeMember(const QString &channelId, const QString &userId)
{
    detail::Statement statement(
        m_db.handle(), "DELETE FROM channel_members WHERE channel_id = ? AND user_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeMember"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, userId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeMember"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<std::optional<ChannelMember>> ChannelsRepo::memberByLogin(const QString &channelId,
                                                                 const QString &login) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT user_id, login, display_name, role, permissions, joined_at"
        " FROM channel_members WHERE channel_id = ? AND login = ? LIMIT 1");
    if (!statement.isOk()) {
        return Result<std::optional<ChannelMember>>::error(
            detail::sqlError(m_db.handle(), "channels.memberByLogin"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, login);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<ChannelMember>>::ok(readMember(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<ChannelMember>>::error(
            detail::sqlError(m_db.handle(), "channels.memberByLogin"));
    }
    return Result<std::optional<ChannelMember>>::ok(std::nullopt);
}

Result<bool> ChannelsRepo::upsertSenderKey(const QString &channelId, const QString &senderId,
                                           const crypto::SenderKeyState &state)
{
    const QByteArray chainAad = channelSenderChainAad(channelId, senderId);
    const QByteArray signingAad = channelSenderSigningAad(channelId, senderId);
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
        R"sql(INSERT INTO channel_sender_keys(channel_id, sender_id, key_id, iteration,
                                              prev_iteration, chain_key, signing_public,
                                              signing_private, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(channel_id, sender_id) DO UPDATE SET
                  key_id = excluded.key_id,
                  iteration = excluded.iteration,
                  prev_iteration = excluded.prev_iteration,
                  chain_key = excluded.chain_key,
                  signing_public = excluded.signing_public,
                  signing_private = CASE WHEN length(excluded.signing_private) > 0
                                         THEN excluded.signing_private
                                         ELSE channel_sender_keys.signing_private END,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.senderKey"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, senderId);
    statement.bindInt64(3, static_cast<qint64>(state.keyId));
    statement.bindInt64(4, static_cast<qint64>(state.iteration));
    statement.bindInt64(5, static_cast<qint64>(state.previousIteration));
    statement.bindBlob(6, sealedChain.value());
    statement.bindBlob(7, state.signingPublic);
    statement.bindBlob(8, sealedSigning);
    statement.bindInt64(9, static_cast<qint64>(state.iteration));
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "channels.senderKey"));
    }
    return Result<bool>::ok(true);
}

Result<std::optional<crypto::SenderKeyState>> ChannelsRepo::senderKey(
    const QString &channelId, const QString &senderId) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT key_id, iteration, prev_iteration, chain_key, signing_public, signing_private"
        " FROM channel_sender_keys WHERE channel_id = ? AND sender_id = ?");
    if (!statement.isOk()) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            detail::sqlError(m_db.handle(), "channels.senderKey"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, senderId);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<std::optional<crypto::SenderKeyState>>::ok(std::nullopt);
    }
    if (rc != SQLITE_ROW) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            detail::sqlError(m_db.handle(), "channels.senderKey"));
    }
    crypto::SenderKeyState state;
    state.keyId = static_cast<quint32>(statement.int64(0));
    state.iteration = static_cast<quint32>(statement.int64(1));
    state.previousIteration = static_cast<quint32>(statement.int64(2));
    state.chainKey = openText(m_cipher, statement.blob(3),
                              channelSenderChainAad(channelId, senderId));
    state.signingPublic = statement.blob(4);
    state.signingPrivate = openText(m_cipher, statement.blob(5),
                                    channelSenderSigningAad(channelId, senderId));
    if (!state.isValid()) {
        return Result<std::optional<crypto::SenderKeyState>>::error(
            QStringLiteral("sender_key_corrupt"));
    }
    return Result<std::optional<crypto::SenderKeyState>>::ok(state);
}

Result<bool> ChannelsRepo::removeSenderKey(const QString &channelId, const QString &senderId)
{
    detail::Statement statement(
        m_db.handle(),
        "DELETE FROM channel_sender_keys WHERE channel_id = ? AND sender_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeSenderKey"));
    }
    statement.bindText(1, channelId);
    statement.bindText(2, senderId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeSenderKey"));
    }
    return Result<bool>::ok(true);
}

Result<bool> ChannelsRepo::removeSenderKeys(const QString &channelId)
{
    detail::Statement statement(
        m_db.handle(), "DELETE FROM channel_sender_keys WHERE channel_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeSenderKeys"));
    }
    statement.bindText(1, channelId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(
            detail::sqlError(m_db.handle(), "channels.removeSenderKeys"));
    }
    return Result<bool>::ok(true);
}

} // namespace mini::storage
