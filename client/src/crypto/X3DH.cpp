#include "crypto/X3DH.h"

#include "crypto/Kdf.h"
#include "crypto/Sodium.h"

#include <sodium.h>

namespace mini::crypto {

namespace {

constexpr int kX25519Bytes = crypto_scalarmult_curve25519_BYTES;
constexpr int kEd25519PublicBytes = crypto_sign_PUBLICKEYBYTES;

QByteArray deepCopy(const QByteArray &source)
{
    if (source.isEmpty()) {
        return {};
    }
    return QByteArray(source.constData(), source.size());
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        sodium_memzero(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }
}

void wipeResult(Result<QByteArray> &result)
{
    if (result.isOk()) {
        wipe(result.value());
    }
}

Result<QByteArray> edPublicToX25519(const QByteArray &edPublic)
{
    if (edPublic.size() != kEd25519PublicBytes) {
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    QByteArray curve(kX25519Bytes, Qt::Uninitialized);
    if (crypto_sign_ed25519_pk_to_curve25519(
            reinterpret_cast<unsigned char *>(curve.data()),
            reinterpret_cast<const unsigned char *>(edPublic.constData()))
        != 0) {
        wipe(curve);
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    return Result<QByteArray>::ok(curve);
}

Result<QByteArray> edSecretToX25519(const QByteArray &edSecret)
{
    if (edSecret.size() != crypto_sign_SECRETKEYBYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    QByteArray curve(kX25519Bytes, Qt::Uninitialized);
    if (crypto_sign_ed25519_sk_to_curve25519(
            reinterpret_cast<unsigned char *>(curve.data()),
            reinterpret_cast<const unsigned char *>(edSecret.constData()))
        != 0) {
        wipe(curve);
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    return Result<QByteArray>::ok(curve);
}

Result<QByteArray> scalarMult(const QByteArray &secret, const QByteArray &publicKey)
{
    if (secret.size() != kX25519Bytes || publicKey.size() != kX25519Bytes) {
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.dh"));
    }
    QByteArray shared(kX25519Bytes, Qt::Uninitialized);
    if (crypto_scalarmult_curve25519(
            reinterpret_cast<unsigned char *>(shared.data()),
            reinterpret_cast<const unsigned char *>(secret.constData()),
            reinterpret_cast<const unsigned char *>(publicKey.constData()))
        != 0) {
        wipe(shared);
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.dh"));
    }
    return Result<QByteArray>::ok(shared);
}

Result<QByteArray> deriveSharedSecret(const QByteArray &initiatorIdentityEd,
                                      const QByteArray &responderIdentityEd,
                                      const QByteArray &dh1, const QByteArray &dh2,
                                      const QByteArray &dh3, const QByteArray &dh4,
                                      bool hasOneTimePrekey)
{
    QByteArray input(32, static_cast<char>(0xFF));
    input.append(dh1);
    input.append(dh2);
    input.append(dh3);
    if (hasOneTimePrekey) {
        input.append(dh4);
    }

    QByteArray info = QByteArrayLiteral("MINI/X3DH/v1");
    info.append(initiatorIdentityEd);
    info.append(responderIdentityEd);

    QByteArray secret = Kdf::hkdfSha256(input, {}, info, X3DH::kSharedSecretBytes);
    wipe(input);
    if (secret.size() != X3DH::kSharedSecretBytes) {
        wipe(secret);
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.kdf"));
    }
    return Result<QByteArray>::ok(secret);
}

} // namespace

Result<X3DHResult> X3DH::initiate(const PrekeyBundle &bundle, const Ed25519KeyPair &identity)
{
    if (!Sodium::init()) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.init"));
    }
    if (!identity.isValid()) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    if (bundle.identityKey.size() != kEd25519PublicBytes
        || bundle.signedPrekey.size() != kX25519Bytes
        || bundle.signedPrekeySignature.size() != crypto_sign_BYTES
        || bundle.signedPrekeyId == 0) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.bundle"));
    }
    if (bundle.oneTimePrekeyId != 0 && bundle.oneTimePrekey.size() != kX25519Bytes) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.bundle"));
    }
    const bool hasOneTimePrekey = bundle.oneTimePrekeyId != 0;
    if (!hasOneTimePrekey && !bundle.oneTimePrekey.isEmpty()) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.bundle"));
    }

    const int signatureCheck = crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char *>(bundle.signedPrekeySignature.constData()),
        reinterpret_cast<const unsigned char *>(bundle.signedPrekey.constData()),
        static_cast<unsigned long long>(bundle.signedPrekey.size()),
        reinterpret_cast<const unsigned char *>(bundle.identityKey.constData()));
    if (signatureCheck != 0) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.signature"));
    }

    X25519KeyPair ephemeral;
    ephemeral.publicKey = QByteArray(crypto_box_PUBLICKEYBYTES, Qt::Uninitialized);
    ephemeral.privateKey = QByteArray(crypto_box_SECRETKEYBYTES, Qt::Uninitialized);
    if (crypto_box_keypair(reinterpret_cast<unsigned char *>(ephemeral.publicKey.data()),
                           reinterpret_cast<unsigned char *>(ephemeral.privateKey.data()))
        != 0) {
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.ephemeral"));
    }

    Result<QByteArray> identitySecret = edSecretToX25519(identity.privateKey);
    Result<QByteArray> peerIdentity = edPublicToX25519(bundle.identityKey);
    if (identitySecret.isError()) {
        return Result<X3DHResult>::error(identitySecret.error());
    }
    if (peerIdentity.isError()) {
        wipe(identitySecret.value());
        return Result<X3DHResult>::error(peerIdentity.error());
    }

    Result<QByteArray> dh1 = scalarMult(identitySecret.value(), bundle.signedPrekey);
    Result<QByteArray> dh2 = scalarMult(ephemeral.privateKey, peerIdentity.value());
    Result<QByteArray> dh3 = scalarMult(ephemeral.privateKey, bundle.signedPrekey);
    Result<QByteArray> dh4 = Result<QByteArray>::ok(QByteArray());
    if (hasOneTimePrekey) {
        dh4 = scalarMult(ephemeral.privateKey, bundle.oneTimePrekey);
    }
    wipe(identitySecret.value());
    wipe(peerIdentity.value());
    if (dh1.isError() || dh2.isError() || dh3.isError() || dh4.isError()) {
        wipeResult(dh1);
        wipeResult(dh2);
        wipeResult(dh3);
        wipeResult(dh4);
        return Result<X3DHResult>::error(QStringLiteral("crypto.x3dh.dh"));
    }

    const Result<QByteArray> sharedSecret =
        deriveSharedSecret(identity.publicKey, bundle.identityKey, dh1.value(), dh2.value(),
                           dh3.value(), hasOneTimePrekey ? dh4.value() : QByteArray(),
                           hasOneTimePrekey);
    wipeResult(dh1);
    wipeResult(dh2);
    wipeResult(dh3);
    wipeResult(dh4);
    if (sharedSecret.isError()) {
        return Result<X3DHResult>::error(sharedSecret.error());
    }

    X3DHResult result;
    result.sharedSecret = sharedSecret.value();
    result.init.identityKey = identity.publicKey;
    result.init.ephemeralKey = ephemeral.publicKey;
    result.init.signedPrekeyId = bundle.signedPrekeyId;
    result.init.oneTimePrekeyId = hasOneTimePrekey ? bundle.oneTimePrekeyId : 0;
    result.ephemeral = ephemeral;
    return Result<X3DHResult>::ok(std::move(result));
}

Result<QByteArray> X3DH::respond(const X3DHInit &init, const IdentityState &state)
{
    if (!Sodium::init()) {
        return Result<QByteArray>::error(QStringLiteral("crypto.init"));
    }
    if (!state.identity.isValid()) {
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.identity"));
    }
    if (init.identityKey.size() != kEd25519PublicBytes
        || init.ephemeralKey.size() != kX25519Bytes || init.signedPrekeyId == 0) {
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.header"));
    }

    const Result<SignedPrekey> signedPrekey = IdentityKeys::signedPrekeyById(
        state, init.signedPrekeyId);
    if (signedPrekey.isError()) {
        return Result<QByteArray>::error(signedPrekey.error());
    }

    const bool hasOneTimePrekey = init.oneTimePrekeyId != 0;
    Result<OneTimePrekey> oneTimePrekey = Result<OneTimePrekey>::ok(OneTimePrekey());
    if (hasOneTimePrekey) {
        oneTimePrekey = IdentityKeys::oneTimePrekeyById(state, init.oneTimePrekeyId);
        if (oneTimePrekey.isError()) {
            return Result<QByteArray>::error(oneTimePrekey.error());
        }
    }

    Result<QByteArray> identitySecret = edSecretToX25519(state.identity.privateKey);
    Result<QByteArray> peerIdentity = edPublicToX25519(init.identityKey);
    if (identitySecret.isError()) {
        return Result<QByteArray>::error(identitySecret.error());
    }
    if (peerIdentity.isError()) {
        wipe(identitySecret.value());
        return Result<QByteArray>::error(peerIdentity.error());
    }

    Result<QByteArray> dh1 = scalarMult(signedPrekey.value().pair.privateKey,
                                        peerIdentity.value());
    Result<QByteArray> dh2 = scalarMult(identitySecret.value(), init.ephemeralKey);
    Result<QByteArray> dh3 = scalarMult(signedPrekey.value().pair.privateKey,
                                        init.ephemeralKey);
    Result<QByteArray> dh4 = Result<QByteArray>::ok(QByteArray());
    if (hasOneTimePrekey) {
        dh4 = scalarMult(oneTimePrekey.value().pair.privateKey, init.ephemeralKey);
    }
    wipe(identitySecret.value());
    wipe(peerIdentity.value());
    if (dh1.isError() || dh2.isError() || dh3.isError() || dh4.isError()) {
        wipeResult(dh1);
        wipeResult(dh2);
        wipeResult(dh3);
        wipeResult(dh4);
        return Result<QByteArray>::error(QStringLiteral("crypto.x3dh.dh"));
    }

    const Result<QByteArray> sharedSecret =
        deriveSharedSecret(init.identityKey, state.identity.publicKey, dh1.value(),
                           dh2.value(), dh3.value(),
                           hasOneTimePrekey ? dh4.value() : QByteArray(),
                           hasOneTimePrekey);
    wipeResult(dh1);
    wipeResult(dh2);
    wipeResult(dh3);
    wipeResult(dh4);
    return sharedSecret;
}

} // namespace mini::crypto
