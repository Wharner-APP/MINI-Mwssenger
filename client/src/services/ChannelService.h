#pragma once

#include "core/Result.h"
#include "crypto/SenderKey.h"
#include "domain/Channel.h"
#include "services/ChannelApi.h"
#include "services/GroupCrypto.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

class MessageService;

// Everything NewChannelDialog collects. Validation mirrors the server:
// 1..128 title, about up to 2000 characters (§43.1).
struct ChannelDraft
{
    QString title;
    QString about;
    QString avatarMedia;
    bool isPublic = false;
    QString linkedGroup;
};

// Channel lifecycle, subscriptions, roles, invites, view counters and the
// channel half of the Sender Keys pipeline (§29.9, §43). Posts are sent
// through MessageService like group text, so a channel is one crypto
// container keyed by channel_id.
class ChannelService : public QObject, public GroupCryptoSession
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit ChannelService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setApi(ChannelApi *api) { m_api = api; }
    void setMessageService(MessageService *messages);
    void setSelfIdentity(const QString &userId, const QString &login);

    [[nodiscard]] static Result<ChannelDraft> validateDraft(const ChannelDraft &draft);
    [[nodiscard]] static QString chatIdFor(const QString &channelId)
    {
        return channel_ids::chatId(channelId);
    }
    [[nodiscard]] static bool canPublish(const Channel &channel)
    {
        return channel.canPublish();
    }

    // Metadata and membership.
    void createChannel(const ChannelDraft &draft, Done<Channel> done);
    void listChannels(Done<QVector<Channel>> done);
    void loadChannel(const QString &channelId, Done<Channel> done);
    void updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                       Done<Channel> done);
    void deleteChannel(const QString &channelId, Done<bool> done);
    void subscribe(const QString &channelId, const QString &token, Done<Channel> done);
    void unsubscribe(const QString &channelId, Done<bool> done);
    void members(const QString &channelId, Done<QVector<ChannelMember>> done);
    void setRole(const QString &channelId, const QString &userId, ChannelRole role,
                 quint32 permissions, Done<ChannelMember> done);
    void createInvite(const QString &channelId, Done<QString> done);
    // §43.5. Returns the view count for admins, -1 when the server hides it.
    void recordView(const QString &channelId, const QString &postId, Done<int> done);

    // Sender keys (§29.9): the admin's chain is distributed to subscribers
    // over pairwise sessions; rotation follows role changes.
    void rotateSenderKey(const QString &channelId, Done<bool> done);
    void distributeSenderKey(const QString &channelId, Done<int> done);
    void handleSenderKeyDistribution(const QString &channelId, const QString &senderLogin,
                                     const QByteArray &payload);

    // GroupCryptoSession.
    void prepareGroup(const QString &channelId,
                      std::function<void(Result<bool>)> done) override;
    void prepareSenderKey(const QString &channelId, const QString &senderLogin,
                          std::function<void(Result<bool>)> done) override;
    [[nodiscard]] bool isGroupReady(const QString &channelId) const override;
    Result<QByteArray> encryptGroup(const QString &channelId, const QByteArray &plaintext,
                                    QJsonObject *header) override;
    Result<QByteArray> decryptGroup(const QString &channelId, const QString &senderLogin,
                                    const QByteArray &ciphertext,
                                    const QJsonObject &header) override;

signals:
    void channelsChanged();
    void channelUpdated(const QString &channelId);
    void senderKeyStored(const QString &channelId, const QString &senderId);
    void errorOccurred(const QString &code, const QString &detail);

private:
    static Channel channelFromInfo(const net::ChannelInfo &info);
    static ChannelMember memberFromInfo(const net::ChannelMemberInfo &info);
    static QString apiError(const net::ApiError &error);

    void persistChannel(const net::ChannelInfo &info, bool withMembers, Done<Channel> done);
    void refreshMembers(const QString &channelId, Done<QVector<ChannelMember>> done);
    void ensureChat(const Channel &channel, Done<bool> done);
    void removeLocalChannel(const QString &channelId, Done<bool> done);
    void loadLocalSenderKey(const QString &channelId,
                            std::function<void(Result<crypto::SenderKeyState>)> done);
    void saveLocalSenderKey(const QString &channelId,
                            const crypto::SenderKeyState &state,
                            std::function<void(bool)> done = {});
    void resolveSenderId(const QString &channelId, const QString &senderLogin,
                         std::function<void(QString)> done);

    storage::StorageWorker *m_storage = nullptr;
    ChannelApi *m_api = nullptr;
    MessageService *m_messages = nullptr;
    QString m_selfUserId;
    QString m_selfLogin;
    QHash<QString, crypto::SenderKeyState> m_localKeys; // channel -> own state
    QHash<QString, crypto::SenderKeyState> m_peerKeys;  // channel|sender -> state
};

} // namespace mini::services
