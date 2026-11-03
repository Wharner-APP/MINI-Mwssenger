#include "crypto/Kdf.h"

#include <sodium.h>

namespace mini::crypto {

namespace {

QByteArray hmacSha256(const QByteArray &key, const QByteArray &message)
{
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state,
                                reinterpret_cast<const unsigned char *>(key.constData()),
                                static_cast<size_t>(key.size()));
    crypto_auth_hmacsha256_update(
        &state, reinterpret_cast<const unsigned char *>(message.constData()),
        static_cast<unsigned long long>(message.size()));
    QByteArray digest(crypto_auth_hmacsha256_BYTES, Qt::Uninitialized);
    crypto_auth_hmacsha256_final(
        &state, reinterpret_cast<unsigned char *>(digest.data()));
    return digest;
}

Result<QByteArray> argon2id(const QByteArray &password, const QByteArray &salt)
{
    if (salt.size() != Kdf::kSaltBytes) {
        return Result<QByteArray>::error(QStringLiteral("kdf.salt"));
    }
    if (sodium_init() < 0) {
        return Result<QByteArray>::error(QStringLiteral("kdf.init"));
    }
    QByteArray key(Kdf::kKeyBytes, Qt::Uninitialized);
    const int rc = crypto_pwhash(
        reinterpret_cast<unsigned char *>(key.data()),
        static_cast<unsigned long long>(key.size()),
        password.constData(), static_cast<unsigned long long>(password.size()),
        reinterpret_cast<const unsigned char *>(salt.constData()),
        Kdf::kOpsLimit, Kdf::kMemoryBytes, crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("kdf.argon2"));
    }
    return Result<QByteArray>::ok(key);
}

} // namespace

QByteArray Kdf::hkdfSha256(const QByteArray &ikm, const QByteArray &salt,
                           const QByteArray &info, int length)
{
    QByteArray prk = hmacSha256(salt, ikm);
    QByteArray okm;
    okm.reserve(length);
    QByteArray previous;
    unsigned char counter = 1;
    while (okm.size() < length) {
        const QByteArray block = previous + info + QByteArray(1, static_cast<char>(counter));
        previous = hmacSha256(prk, block);
        okm.append(previous);
        ++counter;
    }
    sodium_memzero(prk.data(), static_cast<size_t>(prk.size()));
    return okm.left(length);
}

Result<FileKeys> Kdf::deriveFileKeys(const QString &normalized, const QByteArray &saltFile)
{
    const Result<QByteArray> derived = argon2id(normalized.toUtf8(), saltFile);
    if (derived.isError()) {
        return Result<FileKeys>::error(derived.error());
    }

    FileKeys keys;
    keys.kfile = derived.value();
    keys.kauth = hkdfSha256(keys.kfile, {}, QByteArrayLiteral("MINI/auth/v1"));
    keys.kenc = hkdfSha256(keys.kfile, {}, QByteArrayLiteral("MINI/enc/v1"));
    keys.kmac = hkdfSha256(keys.kfile, {}, QByteArrayLiteral("MINI/mac/v1"));
    return Result<FileKeys>::ok(keys);
}

Result<QByteArray> Kdf::derivePasswordKey(const QString &password, const QByteArray &saltPw)
{
    return argon2id(password.toUtf8(), saltPw);
}

Result<QByteArray> Kdf::deriveDbKey(const QByteArray &kenc)
{
    return Result<QByteArray>::ok(
        hkdfSha256(kenc, {}, QByteArrayLiteral("MINI/db/v1")));
}

Result<QByteArray> Kdf::deriveKeyWrap(const QByteArray &kenc)
{
    return Result<QByteArray>::ok(
        hkdfSha256(kenc, {}, QByteArrayLiteral("MINI/keywrap/v1")));
}

} // namespace mini::crypto
