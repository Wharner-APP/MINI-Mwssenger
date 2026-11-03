#pragma once

#include "core/Result.h"
#include "crypto/IdentityKeys.h"

#include <QByteArray>

#include <memory>

namespace mini::crypto {

// Wire header of one ratchet message. It is authenticated as AEAD associated
// data, so tampering with any field makes decryption fail.
struct RatchetHeader
{
    QByteArray dhPublic;          // sender's current X25519 ratchet key, 32
    quint32 previousChainLength = 0; // PN: messages in the previous sending chain
    quint32 messageNumber = 0;       // N: index inside the current sending chain

    static constexpr int kSerializedBytes = 32 + 4 + 4;

    [[nodiscard]] QByteArray serialize() const; // empty when dhPublic is invalid
    [[nodiscard]] static Result<RatchetHeader> parse(const QByteArray &bytes);
};

struct RatchetMessage
{
    RatchetHeader header;
    // XChaCha20-Poly1305 ciphertext with its 16-byte tag. The nonce is not
    // transmitted: every message key is used exactly once, so it is all zero.
    QByteArray ciphertext;

    [[nodiscard]] QByteArray serialize() const;
    [[nodiscard]] static Result<RatchetMessage> parse(const QByteArray &bytes);
};

// Double Ratchet (Signal specification) on top of libsodium:
//   KDF_RK = HKDF-SHA256(salt = rootKey, ikm = DH, info = "MINI/DoubleRatchet/v1")
//   KDF_CK = HMAC-SHA256(chainKey, 0x01) -> messageKey,
//            HMAC-SHA256(chainKey, 0x02) -> next chainKey
// Out-of-order messages are handled with stored skipped message keys; a single
// jump larger than kMaxSkip is rejected with error "crypto_skip_limit".
class DoubleRatchet
{
public:
    static constexpr int kMaxSkip = 1000;
    static constexpr int kMaxSkippedKeys = 2000;

    DoubleRatchet();
    ~DoubleRatchet();
    DoubleRatchet(DoubleRatchet &&other) noexcept;
    DoubleRatchet &operator=(DoubleRatchet &&other) noexcept;
    DoubleRatchet(const DoubleRatchet &) = delete;
    DoubleRatchet &operator=(const DoubleRatchet &) = delete;

    // Initiator: the ephemeral X25519 key pair produced by X3DH becomes the
    // first ratchet key and is also used to derive the initial sending chain.
    [[nodiscard]] static Result<DoubleRatchet> initInitiator(
        const QByteArray &sharedSecret, const X25519KeyPair &ephemeral,
        const QByteArray &peerSignedPrekey);

    // Responder: starts with the signed prekey pair, no chain until the first
    // message arrives.
    [[nodiscard]] static Result<DoubleRatchet> initResponder(
        const QByteArray &sharedSecret, const X25519KeyPair &signedPrekey,
        const QByteArray &initiatorEphemeral);

    // The header encrypt() will attach to its next message. Lets a transport
    // put the header itself into the AEAD associated data.
    [[nodiscard]] RatchetHeader nextHeader() const;

    [[nodiscard]] Result<RatchetMessage> encrypt(const QByteArray &plaintext,
                                                 const QByteArray &associatedData = {});
    [[nodiscard]] Result<QByteArray> decrypt(const RatchetMessage &message,
                                             const QByteArray &associatedData = {});

    // Drops the session and all key material (reinstall / explicit reset).
    void reset();

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] int skippedKeyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mini::crypto
