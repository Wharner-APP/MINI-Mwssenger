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

} // namespace mini::net

Q_DECLARE_METATYPE(mini::net::Envelope)
