#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace mini::crypto {

// Ed25519 identity key pair. `privateKey` is the 64-byte libsodium secret key
// (seed || public key); `publicKey` is its 32-byte counterpart.
struct Ed25519KeyPair
{
    QByteArray publicKey;
    QByteArray privateKey;

    [[nodiscard]] bool isValid() const;
};

// X25519 prekey / ratchet key pair.
struct X25519KeyPair
{
    QByteArray publicKey;
    QByteArray privateKey;

    [[nodiscard]] bool isValid() const;
};

struct SignedPrekey
{
    quint32 id = 0;
    X25519KeyPair pair;
    QByteArray signature; // Ed25519(identity, pair.publicKey), 64 bytes
    qint64 createdAtMs = 0;

    [[nodiscard]] bool isValid() const;
};

struct OneTimePrekey
{
    quint32 id = 0;
    X25519KeyPair pair;

    [[nodiscard]] bool isValid() const;
};

// Public half of a device key set, as uploaded to the server and fetched by a
// peer before X3DH. Private material never appears here.
struct PrekeyBundle
{
    QByteArray identityKey;           // Ed25519 public, 32
    quint32 signedPrekeyId = 0;
    QByteArray signedPrekey;          // X25519 public, 32
    QByteArray signedPrekeySignature; // Ed25519 signature, 64
    quint32 oneTimePrekeyId = 0;      // 0 when the bundle has no one-time prekey
    QByteArray oneTimePrekey;         // X25519 public, 32 or empty
};

// Everything secret a device owns. The destructor wipes private bytes; the
// copy constructor/assignment deep-copy them so a wiped temporary can never
// corrupt a live state through QByteArray's implicit sharing.
struct IdentityState
{
    Ed25519KeyPair identity;
    SignedPrekey signedPrekey;
    QVector<OneTimePrekey> oneTimePrekeys;

    IdentityState() = default;
    IdentityState(const IdentityState &other);
    IdentityState &operator=(const IdentityState &other);
    IdentityState(IdentityState &&other) noexcept = default;
    IdentityState &operator=(IdentityState &&other) noexcept = default;
    ~IdentityState();

    void wipe();

    [[nodiscard]] bool isValid() const;
};

// Device key lifecycle and the AEAD container data/keys.bin.
//
// keys.bin = MAGIC "MINIKB01" | VER u8 | COUNT u16le | entries
// entry    = id u16le | type u8 | plainLen u32le | nonce(24) | ciphertext
// Each entry is XChaCha20-Poly1305 under keyWrap; the AAD binds
// "mini-keys" + deviceId + entry id + total entry count, so a truncated or
// re-ordered file fails authentication instead of silently losing prekeys.
class IdentityKeys
{
public:
    IdentityKeys() = delete;

    static constexpr int kOneTimePrekeyCount = 20;
    static constexpr qint64 kSignedPrekeyLifetimeMs = 30LL * 24 * 60 * 60 * 1000;

    // Fresh identity + signed prekey + 20 one-time prekeys.
    [[nodiscard]] static Result<IdentityState> generate(qint64 nowMs = 0);

    // Creates and persists a new set only when the file does not exist.
    // Never silently replaces an unreadable keys.bin (R299.1).
    [[nodiscard]] static Result<IdentityState> loadOrCreate(const QString &path,
                                                            const QByteArray &keyWrap,
                                                            const QString &deviceId,
                                                            qint64 nowMs = 0);

    // Strict load: missing file is an error, not a reason to generate.
    [[nodiscard]] static Result<IdentityState> load(const QString &path,
                                                    const QByteArray &keyWrap,
                                                    const QString &deviceId);

    [[nodiscard]] static Result<int> save(const IdentityState &state,
                                          const QString &path,
                                          const QByteArray &keyWrap,
                                          const QString &deviceId);

    // Picks a one-time prekey (the first one when id == 0) and returns the
    // public bundle for X3DH.
    [[nodiscard]] static Result<PrekeyBundle> publicBundle(const IdentityState &state,
                                                           quint32 oneTimePrekeyId = 0);

    // Ed25519 signature over an X25519 prekey public key.
    [[nodiscard]] static Result<QByteArray> signPrekey(const IdentityState &state,
                                                       const QByteArray &prekeyPublic);

    // Rotates the signed prekey when it is older than 30 days and tops the
    // one-time prekey pool back up to 20. Consumed prekeys stay consumed.
    [[nodiscard]] static Result<IdentityState> rotatePrekeys(IdentityState state,
                                                             qint64 nowMs = 0);

    // Removes and returns a one-time prekey so it can be used exactly once.
    [[nodiscard]] static Result<OneTimePrekey> consumeOneTimePrekey(IdentityState &state,
                                                                    quint32 id);

    [[nodiscard]] static Result<SignedPrekey> signedPrekeyById(const IdentityState &state,
                                                               quint32 id);
    [[nodiscard]] static Result<OneTimePrekey> oneTimePrekeyById(const IdentityState &state,
                                                                 quint32 id);
};

} // namespace mini::crypto
