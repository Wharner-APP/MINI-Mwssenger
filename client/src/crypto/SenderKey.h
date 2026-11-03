#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

namespace mini::crypto {

// Per-sender chain used for group messages (§29.8). One state per sender per
// group; the sender keeps signingPrivate, participants only signingPublic.
struct SenderKeyState
{
    quint32 keyId = 0;
    QByteArray chainKey;      // 32 bytes, secret
    quint32 iteration = 0;
    quint32 previousIteration = 0;
    QByteArray signingPublic;  // Ed25519 public, 32 bytes
    QByteArray signingPrivate; // Ed25519 secret, 64 bytes (sender only)

    // Message keys skipped because a newer message arrived first. Bounded by
    // kMaxSkipped so a hostile iteration jump cannot grow memory without end.
    QHash<quint32, QByteArray> skipped;

    static constexpr int kMaxSkipped = 1000;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool canSign() const { return signingPrivate.size() == 64; }
};

// Wire header authenticated as AEAD associated data and signed by the sender.
struct SenderKeyHeader
{
    QString groupId;
    QString senderId;
    quint32 keyId = 0;
    quint32 iteration = 0;
    quint32 previousIteration = 0;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<SenderKeyHeader> fromJson(const QJsonObject &object);
    // Deterministic bytes signed and fed to the AEAD. Excludes the signature.
    [[nodiscard]] QByteArray aad() const;
};

// Sender Keys over libsodium:
//   mk        = HMAC-SHA256(chain_key, 0x01)
//   chain_key = HMAC-SHA256(chain_key, 0x02)
//   ct        = XChaCha20-Poly1305(mk, nonce = 0, plaintext, aad = header)
//   signature = Ed25519(header || ciphertext)
class SenderKey
{
public:
    SenderKey() = delete;

    [[nodiscard]] static Result<SenderKeyState> generate(quint32 keyId = 1);
    // New key id and a fresh signing key pair; used on membership changes.
    [[nodiscard]] static Result<SenderKeyState> rotate(const SenderKeyState &current);

    [[nodiscard]] static Result<QByteArray> encrypt(SenderKeyState &state,
                                                    const QString &groupId,
                                                    const QString &senderId,
                                                    const QByteArray &plaintext,
                                                    QJsonObject *headerOut);

    [[nodiscard]] static Result<QByteArray> decrypt(SenderKeyState &state,
                                                    const SenderKeyHeader &header,
                                                    const QByteArray &ciphertext,
                                                    const QByteArray &signature);

    // JSON payload carried inside a pairwise "sender_key_distribution"
    // envelope. Never contains the signing secret.
    [[nodiscard]] static QByteArray serializeDistribution(const QString &groupId,
                                                          const QString &senderId,
                                                          const SenderKeyState &state);
    [[nodiscard]] static Result<SenderKeyState> parseDistribution(
        const QByteArray &payload, const QString &expectedSenderId);

    [[nodiscard]] static QByteArray messageKey(const QByteArray &chainKey);
    [[nodiscard]] static QByteArray nextChainKey(const QByteArray &chainKey);
};

} // namespace mini::crypto
