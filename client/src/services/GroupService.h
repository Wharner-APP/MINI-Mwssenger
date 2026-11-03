#pragma once

#include "core/Result.h"
#include "crypto/SenderKey.h"
#include "domain/Group.h"
#include "services/GroupApi.h"
#include "services/GroupCrypto.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

class MessageService;

// Everything NewGroupDialog collects. Validation mirrors the server:
// 1..128 title, at least one other member.
struct GroupDraft
{
    QString title;
    QString about;
    QStringList members; // logins
    bool isPublic = false;
    bool joinByRequest = false;
    int slowModeSec = 0;
};

// Group roster, roles, invites, join requests, bans, ownership transfer and
// the group half of the message pipeline (Sender Keys, §29.8).
class GroupService : public QObject, public GroupCryptoSession
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit GroupService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setApi(GroupApi *api) { m_api = api; }
    void setMessageService(MessageService *messages);
    void setSelfIdentity(const QString &userId, const QString &login);

    [[nodiscard]] static Result<GroupDraft> validateDraft(const GroupDraft &draft);
    [[nodiscard]] static QString chatIdFor(const QString &groupId)
    {
        return group_ids::chatId(groupId);
    }

    // Roster and metadata.
    void createGroup(const GroupDraft &draft, Done<Group> done);
    void listGroups(Done<QVector<Group>> done);
    void loadGroup(const QString &groupId, Done<Group> done);
    void addMember(const QString &groupId, const QString &login, Done<GroupMember> done);
    void removeMember(const QString &groupId, const QString &userId, bool ban,
                      Done<bool> done);
    void leaveGroup(const QString &groupId, Done<bool> done);
    void setRole(const QString &groupId, const QString &userId, GroupRole role,
                 quint32 permissions, Done<GroupMember> done);
    void transferOwnership(const QString &groupId, const QString &userId, Done<Group> done);
    void createInvite(const QString &groupId, Done<QString> done);
    void joinByToken(const QString &token, Done<Group> done);
    void requestJoin(const QString &groupId, Done<bool> done);
    void listRequests(const QString &groupId, Done<QVector<GroupJoinRequest>> done);
    void approveRequest(const QString &groupId, const QString &userId, bool approve,
                        Done<bool> done);
    void banMember(const QString &groupId, const QString &userId, bool permanent,
                   Done<bool> done);
    void unbanMember(const QString &groupId, const QString &userId, Done<bool> done);

    // Sender keys (§29.8): rotate after a member leaves, is removed, or is
    // promoted; distribute the current chain to every member over pairwise.
    void rotateSenderKey(const QString &groupId, Done<bool> done);
    void distributeSenderKey(const QString &groupId, Done<int> done);
    void handleSenderKeyDistribution(const QString &groupId, const QString &senderLogin,
                                     const QByteArray &payload);

    // GroupCryptoSession.
    void prepareGroup(const QString &groupId,
                      std::function<void(Result<bool>)> done) override;
    void prepareSenderKey(const QString &groupId, const QString &senderLogin,
                          std::function<void(Result<bool>)> done) override;
    [[nodiscard]] bool isGroupReady(const QString &groupId) const override;
    Result<QByteArray> encryptGroup(const QString &groupId, const QByteArray &plaintext,
                                    QJsonObject *header) override;
    Result<QByteArray> decryptGroup(const QString &groupId, const QString &senderLogin,
                                    const QByteArray &ciphertext,
                                    const QJsonObject &header) override;

signals:
    void groupsChanged();
    void groupUpdated(const QString &groupId);
    void senderKeyStored(const QString &groupId, const QString &senderId);
    void errorOccurred(const QString &code, const QString &detail);

private:
    static Group groupFromInfo(const net::GroupInfo &info);
    static GroupMember memberFromInfo(const net::GroupMemberInfo &info);
    static QString apiError(const net::ApiError &error);

    void persistGroup(const net::GroupInfo &info, bool withMembers,
                      Done<Group> done);
    void ensureChat(const Group &group, Done<bool> done);
    void removeLocalGroup(const QString &groupId, Done<bool> done);
    void loadLocalSenderKey(const QString &groupId,
                            std::function<void(Result<crypto::SenderKeyState>)> done);
    void saveLocalSenderKey(const QString &groupId,
                            const crypto::SenderKeyState &state,
                            std::function<void(bool)> done = {});
    void resolveSenderId(const QString &groupId, const QString &senderLogin,
                         std::function<void(QString)> done);
    void cleanupAfterMembershipChange(const QString &groupId, Done<bool> done);

    storage::StorageWorker *m_storage = nullptr;
    GroupApi *m_api = nullptr;
    MessageService *m_messages = nullptr;
    QString m_selfUserId;
    QString m_selfLogin;
    QHash<QString, crypto::SenderKeyState> m_localKeys; // group -> own state
    QHash<QString, crypto::SenderKeyState> m_peerKeys;  // group|sender -> state
};

} // namespace mini::services
