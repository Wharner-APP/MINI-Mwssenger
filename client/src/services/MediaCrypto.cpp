#include "services/MediaCrypto.h"

#include "crypto/Sodium.h"

#include <QFile>
#include <QtEndian>

#include <sodium.h>

namespace mini::services {

namespace {

QByteArray nonceFor(const QByteArray &nonceBase, int index)
{
    QByteArray nonce = nonceBase.left(16);
    nonce.reserve(24);
    uchar tail[8];
    qToBigEndian<quint64>(static_cast<quint64>(index), tail);
    nonce.append(reinterpret_cast<const char *>(tail), 8);
    return nonce;
}

QByteArray aadFor(const QString &mediaId, int index)
{
    QByteArray aad = mediaId.toUtf8();
    uchar tail[8];
    qToBigEndian<quint64>(static_cast<quint64>(index), tail);
    aad.append(reinterpret_cast<const char *>(tail), 8);
    return aad;
}

} // namespace

QByteArray MediaCrypto::randomKey()
{
    return crypto::Sodium::randomBytes(32);
}

QByteArray MediaCrypto::randomNonce()
{
    return crypto::Sodium::randomBytes(16);
}

Result<QByteArray> MediaCrypto::encryptChunk(const QByteArray &key,
                                             const QByteArray &nonceBase,
                                             const QString &mediaId, int index,
                                             const QByteArray &plain)
{
    if (!crypto::Sodium::init()) {
        return Result<QByteArray>::error(QStringLiteral("crypto_unavailable"));
    }
    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES
        || nonceBase.size() != 16) {
        return Result<QByteArray>::error(QStringLiteral("media_key_invalid"));
    }
    const QByteArray nonce = nonceFor(nonceBase, index);
    const QByteArray aad = aadFor(mediaId, index);
    QByteArray cipher(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long cipherLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(cipher.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(plain.constData()),
            static_cast<unsigned long long>(plain.size()),
            reinterpret_cast<const unsigned char *>(aad.constData()),
            static_cast<unsigned long long>(aad.size()), nullptr,
            reinterpret_cast<const unsigned char *>(nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()))
        != 0) {
        return Result<QByteArray>::error(QStringLiteral("media_encrypt_failed"));
    }
    cipher.resize(static_cast<int>(cipherLen));
    return Result<QByteArray>::ok(cipher);
}

Result<QByteArray> MediaCrypto::decryptChunk(const QByteArray &key,
                                             const QByteArray &nonceBase,
                                             const QString &mediaId, int index,
                                             const QByteArray &cipher)
{
    if (!crypto::Sodium::init()) {
        return Result<QByteArray>::error(QStringLiteral("crypto_unavailable"));
    }
    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES
        || nonceBase.size() != 16 || cipher.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return Result<QByteArray>::error(QStringLiteral("media_key_invalid"));
    }
    const QByteArray nonce = nonceFor(nonceBase, index);
    const QByteArray aad = aadFor(mediaId, index);
    QByteArray plain(cipher.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(plain.data()), &plainLen, nullptr,
            reinterpret_cast<const unsigned char *>(cipher.constData()),
            static_cast<unsigned long long>(cipher.size()),
            reinterpret_cast<const unsigned char *>(aad.constData()),
            static_cast<unsigned long long>(aad.size()),
            reinterpret_cast<const unsigned char *>(nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()))
        != 0) {
        return Result<QByteArray>::error(QStringLiteral("media_decrypt_failed"));
    }
    plain.resize(static_cast<int>(plainLen));
    return Result<QByteArray>::ok(plain);
}

Result<QString> MediaCrypto::hashFile(const QString &path)
{
    if (!crypto::Sodium::init()) {
        return Result<QString>::error(QStringLiteral("crypto_unavailable"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QString>::error(QStringLiteral("file_unreadable"));
    }
    crypto_generichash_state state;
    if (crypto_generichash_init(&state, nullptr, 0, 32) != 0) {
        return Result<QString>::error(QStringLiteral("hash_init_failed"));
    }
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) {
            return Result<QString>::error(QStringLiteral("file_read_failed"));
        }
        if (crypto_generichash_update(
                &state, reinterpret_cast<const unsigned char *>(block.constData()),
                static_cast<unsigned long long>(block.size()))
            != 0) {
            return Result<QString>::error(QStringLiteral("hash_update_failed"));
        }
    }
    unsigned char digest[32];
    if (crypto_generichash_final(&state, digest, sizeof digest) != 0) {
        return Result<QString>::error(QStringLiteral("hash_final_failed"));
    }
    return Result<QString>::ok(
        QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(digest),
                                       static_cast<int>(sizeof digest))
                                .toHex()));
}

Result<QString> MediaCrypto::hashBytes(const QByteArray &bytes)
{
    if (!crypto::Sodium::init()) {
        return Result<QString>::error(QStringLiteral("crypto_unavailable"));
    }
    unsigned char digest[32];
    if (crypto_generichash(digest, sizeof digest,
                           reinterpret_cast<const unsigned char *>(bytes.constData()),
                           static_cast<unsigned long long>(bytes.size()), nullptr, 0)
        != 0) {
        return Result<QString>::error(QStringLiteral("hash_failed"));
    }
    return Result<QString>::ok(
        QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(digest),
                                       static_cast<int>(sizeof digest))
                                .toHex()));
}

int MediaCrypto::chunkCount(qint64 plainSize)
{
    if (plainSize <= 0) {
        return 0;
    }
    return static_cast<int>((plainSize + kChunkPlain - 1) / kChunkPlain);
}

qint64 MediaCrypto::cipherSize(qint64 plainSize)
{
    return plainSize + static_cast<qint64>(chunkCount(plainSize))
        * crypto_aead_xchacha20poly1305_ietf_ABYTES;
}

} // namespace mini::services
