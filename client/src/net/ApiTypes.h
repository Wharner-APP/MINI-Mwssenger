#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

namespace mini::net {

// Wire contract of the HTTP API. JSON keys are snake_case; these value types
// keep the rest of the client away from raw JSON.

struct AuthPrepare {
    QString login;
    QByteArray saltFile;
    QByteArray saltPw;
    QString kdf = QStringLiteral("argon2id");
    int memoryKiB = 65536;
    int iterations = 3;
    int parallelism = 1;
    int keyLength = 32;
};

struct LoginAvailability {
    QString login;
    bool available = false;
    QString reason;
};

struct DeviceInfo {
    QString deviceId;
    QString name;
    QString ip;
    qint64 lastActiveAt = 0;
};

struct AccountProfile {
    QString userId;
    QString login;
    QString displayName;
    QString bio;
    qint64 createdAt = 0;
    bool online = false;
    qint64 lastActiveAt = 0;
    QList<DeviceInfo> devices;
};

struct AuthSession {
    QString accessToken;
    QString refreshToken;
    QString deviceId;
    qint64 accessExpiresAt = 0;
    qint64 refreshExpiresAt = 0;
    AccountProfile user;
};

struct RegisterRequest {
    QString login;
    QString displayName;
    QByteArray authKey;     // K_auth, sent only inside TLS
    QByteArray passwordKey; // PW_KEY, sent only inside TLS
    QString nonce;          // single-use slot from /auth/prepare
    QString captchaTicket;
    QString deviceId;
    QString deviceName;
    QByteArray identityPublicKey;
    QByteArray signedPrekeyPublic;
    QByteArray signedPrekeySignature;
    QList<QByteArray> oneTimePrekeys;
};

struct LoginRequest {
    QString login;
    QByteArray authKey;
    QByteArray passwordKey;
    QString nonce;
    QString deviceId;
    QString deviceName;
};

struct Envelope {
    QString kind;
    QString clientMsgId;
    QString chatId;
    QByteArray ciphertext;
    QJsonObject header;
};

struct MailItem {
    QString mailId;
    qint64 receivedAt = 0;
    Envelope envelope;
};

struct MailBatch {
    QList<MailItem> items;
    QString cursor;
    bool hasMore = false;
};

struct SendResult {
    QStringList accepted;
    QStringList rejected;
};

// Media staging (§30.5.2): the server is an opaque relay for ciphertext.
struct MediaUploadSession {
    QString uploadId;
    qint64 totalSize = 0;
    qint64 chunkSize = 0;
    int chunkCount = 0;
    qint64 expiresAt = 0;
};

struct MediaChunkResult {
    QString uploadId;
    int idx = -1;
    qint64 received = 0;
    int receivedChunks = 0;
    int chunkCount = 0;
};

struct MediaCompleteResult {
    QString uploadId;
    qint64 totalSize = 0;
    int chunkCount = 0;
    qint64 completedAt = 0;
};

struct AckResult {
    QStringList acked;
};

struct WsTicket {
    QString ticket;
    int expiresIn = 300;
};

struct ProfilePatch {
    std::optional<QString> displayName;
    std::optional<QString> bio;
};

// Public half of a device key set, published under /api/v1/mail/keys.
struct PeerKeys {
    QString login;
    QString deviceId;
    QByteArray identityKey;           // Ed25519 public, 32
    QByteArray signedPrekey;          // X25519 public, 32
    QByteArray signedPrekeySignature; // Ed25519 signature, 64
    quint32 signedPrekeyId = 0;
    // Published bundle: every fresh one-time prekey of this device.
    QList<QPair<quint32, QByteArray>> oneTimePrekeys; // id -> X25519 public
    // Fetched bundle: the single prekey the server handed out (0 = none).
    quint32 oneTimePrekeyId = 0;
    QByteArray oneTimePrekey;         // X25519 public, 32 or empty
};

struct ChangePasswordRequest {
    QByteArray oldPasswordKey;
    QByteArray newPasswordKey;
    QByteArray newAuthKey;
};

struct ReissueRequest {
    QString login;
    QString nonce;
    QByteArray newAuthKey;
    QByteArray newPasswordKey;
};

struct ReissueResult {
    QByteArray saltFile;
    QByteArray saltPw;
    QString kdf = QStringLiteral("argon2id");
    int memoryKiB = 65536;
    int iterations = 3;
    int parallelism = 1;
    int keyLength = 32;
    AuthSession session;
};

struct DeleteAccountRequest {
    QString login;
    QByteArray passwordKey;
    QString confirmation;
};

// ---- Groups (§30.5, §42) ------------------------------------------------

struct GroupMemberInfo {
    QString userId;
    QString login;
    QString displayName;
    QString role = QStringLiteral("member");
    quint32 permissions = 0;
    qint64 joinedAt = 0;
    qint64 mutedUntil = 0;
};

struct GroupInfo {
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
    QString myRole;
    quint32 myPermissions = 0;
    int memberCount = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QList<GroupMemberInfo> members;
    QString requestStatus;
};

struct GroupCreateRequest {
    QString title;
    QString about;
    QString avatarMedia;
    bool isPublic = false;
    bool joinByRequest = false;
    int slowModeSec = 0;
    QStringList members; // user ids or logins
};

struct GroupPatch {
    std::optional<QString> title;
    std::optional<QString> about;
    std::optional<QString> avatarMedia;
    std::optional<bool> isPublic;
    std::optional<bool> joinByRequest;
    std::optional<int> slowModeSec;
};

struct GroupJoinRequestInfo {
    QString groupId;
    QString userId;
    QString login;
    QString displayName;
    QString message;
    QString status = QStringLiteral("pending");
    qint64 createdAt = 0;
};

struct GroupInvite {
    QString groupId;
    QString token;
    QString link;
};

struct GroupJoinResult {
    QString status; // joined | pending | already_member
    GroupInfo group;
};

// ---- Channels (§30.5, §43) ---------------------------------------------

struct ChannelMemberInfo {
    QString userId;
    QString login;
    QString displayName;
    QString role = QStringLiteral("subscriber");
    quint32 permissions = 0;
    qint64 joinedAt = 0;
};

struct ChannelInfo {
    QString channelId;
    QString title;
    QString about;
    QString avatarMedia;
    QString ownerId;
    bool isPublic = false;
    QString linkedGroup;
    QString inviteToken;
    QString inviteLink;
    QString myRole; // empty when the caller is not subscribed
    quint32 myPermissions = 0;
    int subscriberCount = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QList<ChannelMemberInfo> members;
};

struct ChannelCreateRequest {
    QString title;
    QString about;
    QString avatarMedia;
    bool isPublic = false;
    QString linkedGroup;
};

struct ChannelPatch {
    std::optional<QString> title;
    std::optional<QString> about;
    std::optional<QString> avatarMedia;
    std::optional<bool> isPublic;
    std::optional<QString> linkedGroup;
};

struct ChannelInvite {
    QString channelId;
    QString token;
    QString link;
};

struct ChannelViewInfo {
    QString status;
    QString channelId;
    QString postId;
    bool recorded = false;
    int views = -1;
};

// ---- Bots (§30.5.4, §44) ------------------------------------------------

struct BotInfo {
    QString botId;
    QString userId;
    QString login;
    QString displayName;
    QString about;
    QString avatarMedia;
    QString tokenPrefix;
    bool canJoinGroups = true;
    bool canReadAllGroupMessages = false;
    bool inlineEnabled = false;
    bool isBot = true;
    // §29.14: a bot must read the message to answer, so this chat is never E2E.
    bool e2e = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct BotCreateRequest {
    QString login;
    QString displayName;
    QString about;
    bool canJoinGroups = true;
    bool canReadAllGroupMessages = false;
    bool inlineEnabled = false;
};

struct BotCreateResult {
    BotInfo bot;
    QString token;
    bool tokenShownOnce = true;
};

struct BotTokenResult {
    QString token;
    QString tokenPrefix;
    bool tokenShownOnce = true;
};

struct BotSendResult {
    QString messageId;
    QString updateId;
};

// ---- Privacy and sessions (§11.12 #177-182, §28.16) ---------------------

struct PrivacySettings {
    QString whoCanMessage = QStringLiteral("everyone");
    QString whoCanCall = QStringLiteral("everyone");
    QString whoSeesLastSeen = QStringLiteral("everyone");
    bool readReceipts = true;
};

struct BlockedUserInfo {
    QString userId;
    QString login;
    QString displayName;
    qint64 createdAt = 0;
};

struct DeviceRecord {
    QString deviceId;
    QString name;
    QString platform;
    QString ip;
    qint64 createdAt = 0;
    qint64 lastSeenAt = 0;
    bool current = false;
};

struct RevokeAllResult {
    int revokedDevices = 0;
    int revokedSessions = 0;
};

} // namespace mini::net

Q_DECLARE_METATYPE(mini::net::Envelope)
