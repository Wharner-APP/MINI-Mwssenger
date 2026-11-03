#pragma once

#include "core/Result.h"
#include "crypto/IdentityKeys.h"

#include <QByteArray>

namespace mini::crypto {

// Clear-text X3DH preamble that accompanies the first ratchet message.
// It contains only public keys: the initiator's Ed25519 identity and the
// X25519 ephemeral key, plus the prekey ids the responder used.
struct X3DHInit
{
    QByteArray identityKey;  // IK_A, Ed25519 public, 32
    QByteArray ephemeralKey; // EK_A, X25519 public, 32
    quint32 signedPrekeyId = 0;
    quint32 oneTimePrekeyId = 0; // 0 when the bundle had no one-time prekey
};

struct X3DHResult
{
    QByteArray sharedSecret; // 32 bytes, input to the Double Ratchet
    X3DHInit init;           // goes on the wire with the first message
    X25519KeyPair ephemeral; // initiator's ratchet key (responder leaves empty)
};

// X3DH over libsodium primitives:
//   DH1 = DH(IK_A, SPK_B)   DH2 = DH(EK_A, IK_B)
//   DH3 = DH(EK_A, SPK_B)   DH4 = DH(EK_A, OPK_B)
//   SK  = HKDF-SHA256(0xFF*32 || DH1 || DH2 || DH3 [|| DH4],
//                     info = "MINI/X3DH/v1" || IK_A || IK_B)
// The signed prekey signature is verified before any DH is performed.
class X3DH
{
public:
    X3DH() = delete;

    static constexpr int kSharedSecretBytes = 32;

    // Initiator: verifies the bundle signature and derives SK plus the header
    // to send with the first message. No state is modified.
    [[nodiscard]] static Result<X3DHResult> initiate(const PrekeyBundle &bundle,
                                                     const Ed25519KeyPair &identity);

    // Responder: looks the named prekeys up in `state` and derives the same SK.
    [[nodiscard]] static Result<QByteArray> respond(const X3DHInit &init,
                                                    const IdentityState &state);
};

} // namespace mini::crypto
