#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"
#include "net/Endpoint.h"

#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mini::net {

class TlsPin;

// Typed, asynchronous HTTP client. Every method reports via a callback and
// never throws; no JSON key or raw body leaves this class.
class ApiClient : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Callback = std::function<void(ApiResult<T>)>;

    explicit ApiClient(const Endpoint &endpoint, TlsPin *pin = nullptr,
                       QObject *parent = nullptr);
    ApiClient(const Endpoint &endpoint, TlsPin *pin,
              QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ApiClient() override;

    void setEndpoint(const Endpoint &endpoint) { m_endpoint = endpoint; }
    [[nodiscard]] Endpoint endpoint() const { return m_endpoint; }

    void setTlsPin(TlsPin *pin) { m_pin = pin; }
    [[nodiscard]] TlsPin *tlsPin() const { return m_pin; }

    void setAccessToken(const QString &token) { m_accessToken = token; }
    [[nodiscard]] QString accessToken() const { return m_accessToken; }

    void setRefreshToken(const QString &token) { m_refreshToken = token; }
    [[nodiscard]] QString refreshToken() const { return m_refreshToken; }

    void setDeviceId(const QString &deviceId) { m_deviceId = deviceId; }
    [[nodiscard]] QString deviceId() const { return m_deviceId; }

    void setProtocolVersion(int version);
    [[nodiscard]] int protocolVersion() const { return m_protocolVersion; }

    // Primarily for tests: lowers the 15 s HTTP timeout.
    void setTransferTimeoutMs(int milliseconds) { m_transferTimeoutMs = milliseconds; }

    void prepare(const QString &login, Callback<AuthPrepare> callback);
    void checkLogin(const QString &login, Callback<LoginAvailability> callback);
    void registerAccount(const RegisterRequest &request, Callback<AuthSession> callback);
    void login(const LoginRequest &request, Callback<AuthSession> callback);
    void sendEnvelopes(const QList<Envelope> &envelopes, Callback<SendResult> callback);
    void pullMail(const QString &deviceId, const QString &cursor, Callback<MailBatch> callback);
    void ackMail(const QStringList &mailIds, Callback<AckResult> callback);
    void getMe(Callback<AccountProfile> callback);
    void patchMe(const ProfilePatch &patch, Callback<AccountProfile> callback);
    void changePassword(const ChangePasswordRequest &request, Callback<Empty> callback);
    void reissueSignIn(const ReissueRequest &request, Callback<ReissueResult> callback);
    void deleteAccount(const DeleteAccountRequest &request, Callback<Empty> callback);
    void requestWsTicket(Callback<WsTicket> callback);

    // Public prekey directory used by the per-chat session key.
    void putPrekeys(const PeerKeys &keys, Callback<Empty> callback);
    void fetchPeerKeys(const QString &login, Callback<PeerKeys> callback);
    // Exact-login public profile (GET /api/v1/users/{login}).
    void lookupUser(const QString &login, Callback<AccountProfile> callback);

    // Groups (§30.5). Every call is authenticated; member identifiers are the
    // server's user ids, except create/add which also accept logins.
    void listGroups(Callback<QList<GroupInfo>> callback);
    void createGroup(const GroupCreateRequest &request, Callback<GroupInfo> callback);
    void getGroup(const QString &groupId, Callback<GroupInfo> callback);
    void updateGroup(const QString &groupId, const GroupPatch &patch,
                     Callback<GroupInfo> callback);
    void addGroupMember(const QString &groupId, const QString &userIdOrLogin,
                        Callback<GroupMemberInfo> callback);
    void removeGroupMember(const QString &groupId, const QString &userId, bool ban,
                           int durationHours, Callback<Empty> callback);
    void setGroupRole(const QString &groupId, const QString &userId, const QString &role,
                      quint32 permissions, Callback<GroupMemberInfo> callback);
    void transferGroupOwnership(const QString &groupId, const QString &userId,
                                Callback<GroupInfo> callback);
    void createGroupInvite(const QString &groupId, Callback<GroupInvite> callback);
    void joinGroup(const QString &token, const QString &message,
                   Callback<GroupJoinResult> callback);
    void requestGroupJoin(const QString &groupId, const QString &message,
                          Callback<GroupJoinRequestInfo> callback);
    void listGroupRequests(const QString &groupId, const QString &status,
                           Callback<QList<GroupJoinRequestInfo>> callback);
    void approveGroupRequest(const QString &groupId, const QString &userId, bool approve,
                             Callback<GroupJoinRequestInfo> callback);
    void banGroupMember(const QString &groupId, const QString &userId, bool permanent,
                        int durationHours, const QString &reason, Callback<Empty> callback);
    void unbanGroupMember(const QString &groupId, const QString &userId,
                          Callback<Empty> callback);

    // Channels (§30.5, §43). A channel is a broadcast container; the server
    // never sees post bodies, only the (channel, post, user) view fact.
    void listChannels(Callback<QList<ChannelInfo>> callback);
    void createChannel(const ChannelCreateRequest &request, Callback<ChannelInfo> callback);
    void getChannel(const QString &channelId, Callback<ChannelInfo> callback);
    void updateChannel(const QString &channelId, const ChannelPatch &patch,
                       Callback<ChannelInfo> callback);
    void deleteChannel(const QString &channelId, Callback<Empty> callback);
    void subscribeChannel(const QString &channelId, const QString &token,
                          Callback<QString> callback);
    void unsubscribeChannel(const QString &channelId, Callback<QString> callback);
    void listChannelMembers(const QString &channelId,
                            Callback<QList<ChannelMemberInfo>> callback);
    void setChannelRole(const QString &channelId, const QString &userId,
                        const QString &role, quint32 permissions,
                        Callback<ChannelMemberInfo> callback);
    void createChannelInvite(const QString &channelId, Callback<ChannelInvite> callback);
    void recordChannelView(const QString &channelId, const QString &postId,
                           Callback<ChannelViewInfo> callback);

    // Bots (§30.5.4, §44). The token is returned only by create/rotate; the
    // server stores a hash and a prefix, so the client never persists it.
    void listBots(Callback<QList<BotInfo>> callback);
    void createBot(const BotCreateRequest &request, Callback<BotCreateResult> callback);
    void getBot(const QString &botId, Callback<BotInfo> callback);
    void rotateBotToken(const QString &botId, Callback<BotTokenResult> callback);
    void deleteBot(const QString &botId, Callback<Empty> callback);
    void sendBotMessage(const QString &botId, const QString &text,
                        Callback<BotSendResult> callback);

    // Privacy, block list and devices (§11.12 #177-182, §28.16).
    void getPrivacySettings(Callback<PrivacySettings> callback);
    void updatePrivacySettings(const PrivacySettings &settings,
                               Callback<PrivacySettings> callback);
    void listBlocks(Callback<QList<BlockedUserInfo>> callback);
    void blockUser(const QString &userOrLogin, Callback<Empty> callback);
    void unblockUser(const QString &userId, Callback<Empty> callback);
    void listDevices(Callback<QList<DeviceRecord>> callback);
    void revokeDevice(const QString &deviceId, Callback<Empty> callback);
    void revokeAllDevices(Callback<RevokeAllResult> callback);

    // Encrypted media staging (§29.11, §30.5.2). Chunks are raw ciphertext,
    // not JSON: never log or re-encode them.
    void mediaInit(qint64 totalSize, qint64 chunkSize,
                   Callback<MediaUploadSession> callback);
    void mediaPutChunk(const QString &uploadId, int idx, const QByteArray &data,
                       Callback<MediaChunkResult> callback);
    void mediaComplete(const QString &uploadId, Callback<MediaCompleteResult> callback);
    void mediaGetChunk(const QString &uploadId, int idx, Callback<QByteArray> callback);

signals:
    // A 401 from any endpoint. The caller must stop retrying and sign in again.
    void authenticationRequired();
    void certificateMismatch(const QString &actual, const QString &expected);
    // Warning only (HTTPS): the pin is missing or malformed, so the request
    // relies on CA validation. WSS treats the same condition as a failure.
    void certificateWarning(const QString &reason, const QString &expected);
    void protocolVersionMismatch(int serverVersion);

private:
    using RawCallback = std::function<void(ApiResult<QByteArray>)>;

    void send(const QByteArray &method, const QString &path, const QJsonObject &body,
              bool authenticated, RawCallback callback);
    // Transport primitive: builds the pinned request and maps the reply to
    // either raw bytes or an ApiError. JSON parsing stays in the callers.
    void perform(const QByteArray &method, const QString &path,
                 const QByteArray &payload, const QByteArray &contentType,
                 bool authenticated, RawCallback callback);
    [[nodiscard]] ApiError tlsErrorFor(QNetworkReply *reply) const;

    Endpoint m_endpoint;
    TlsPin *m_pin = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    bool m_ownsNetwork = false;
    QString m_accessToken;
    QString m_refreshToken;
    QString m_deviceId;
    int m_protocolVersion = 1;
    int m_transferTimeoutMs = 15000;
};

} // namespace mini::net
