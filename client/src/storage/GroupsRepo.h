#pragma once

#include "core/Result.h"
#include "crypto/SenderKey.h"
#include "domain/Group.h"
#include "storage/BodyCipher.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

// Must only be called on the StorageWorker thread.
class GroupsRepo
{
public:
    explicit GroupsRepo(Database &db);

    Result<QVector<Group>> listGroups() const;
    Result<std::optional<Group>> getGroup(const QString &groupId) const;
    Result<bool> upsertGroup(const Group &group);
    Result<bool> removeGroup(const QString &groupId);

    Result<QVector<GroupMember>> members(const QString &groupId) const;
    Result<bool> replaceMembers(const QString &groupId, const QVector<GroupMember> &members);
    Result<bool> upsertMember(const QString &groupId, const GroupMember &member);
    Result<bool> removeMember(const QString &groupId, const QString &userId);
    Result<std::optional<GroupMember>> memberByLogin(const QString &groupId,
                                                     const QString &login) const;

    // Sender keys. chain_key and signing_private are sealed under DB_KEY; the
    // public key and counters stay readable so a row can be inspected.
    Result<bool> upsertSenderKey(const QString &groupId, const QString &senderId,
                                 const crypto::SenderKeyState &state);
    Result<std::optional<crypto::SenderKeyState>> senderKey(const QString &groupId,
                                                            const QString &senderId) const;
    Result<bool> removeSenderKey(const QString &groupId, const QString &senderId);
    Result<bool> removeSenderKeys(const QString &groupId);

private:
    Database &m_db;
    BodyCipher m_cipher;
};

// AAD helpers keep a sealed chain key bound to its group and sender.
[[nodiscard]] QByteArray groupSenderChainAad(const QString &groupId,
                                             const QString &senderId);
[[nodiscard]] QByteArray groupSenderSigningAad(const QString &groupId,
                                               const QString &senderId);

} // namespace mini::storage
