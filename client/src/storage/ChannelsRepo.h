#pragma once

#include "core/Result.h"
#include "crypto/SenderKey.h"
#include "domain/Channel.h"
#include "storage/BodyCipher.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

// Local cache of server channel state, subscriptions and the channel half of
// the sender-key store. Must only be called on the StorageWorker thread.
class ChannelsRepo
{
public:
    explicit ChannelsRepo(Database &db);

    Result<QVector<Channel>> listChannels() const;
    Result<std::optional<Channel>> getChannel(const QString &channelId) const;
    Result<bool> upsertChannel(const Channel &channel);
    Result<bool> removeChannel(const QString &channelId);

    Result<QVector<ChannelMember>> members(const QString &channelId) const;
    Result<bool> replaceMembers(const QString &channelId,
                                const QVector<ChannelMember> &members);
    Result<bool> upsertMember(const QString &channelId, const ChannelMember &member);
    Result<bool> removeMember(const QString &channelId, const QString &userId);
    Result<std::optional<ChannelMember>> memberByLogin(const QString &channelId,
                                                       const QString &login) const;

    // chain_key and signing_private are sealed under DB_KEY.
    Result<bool> upsertSenderKey(const QString &channelId, const QString &senderId,
                                 const crypto::SenderKeyState &state);
    Result<std::optional<crypto::SenderKeyState>> senderKey(const QString &channelId,
                                                            const QString &senderId) const;
    Result<bool> removeSenderKey(const QString &channelId, const QString &senderId);
    Result<bool> removeSenderKeys(const QString &channelId);

private:
    Database &m_db;
    BodyCipher m_cipher;
};

// AAD helpers keep a sealed chain key bound to its channel and sender.
[[nodiscard]] QByteArray channelSenderChainAad(const QString &channelId,
                                               const QString &senderId);
[[nodiscard]] QByteArray channelSenderSigningAad(const QString &channelId,
                                                 const QString &senderId);

} // namespace mini::storage
