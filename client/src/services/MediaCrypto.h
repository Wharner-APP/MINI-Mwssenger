#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace mini::services {

// §29.11: each file gets its own random key; chunks are sealed with
// XChaCha20-Poly1305. The 16-byte nonce base plus a big-endian chunk index
// forms the 24-byte nonce, and file_id || index is the AAD, so a chunk cannot
// be replayed into another file or position.
//
// The server expects every ciphertext chunk except the last to be exactly
// 1 MiB, so the plaintext chunk is 1 MiB minus the 16-byte Poly1305 tag.
class MediaCrypto
{
public:
    static constexpr int kChunkPlain = 1024 * 1024 - 16;
    static constexpr int kChunkCipher = 1024 * 1024;

    [[nodiscard]] static QByteArray randomKey();
    [[nodiscard]] static QByteArray randomNonce();

    [[nodiscard]] static Result<QByteArray> encryptChunk(const QByteArray &key,
                                                         const QByteArray &nonceBase,
                                                         const QString &mediaId, int index,
                                                         const QByteArray &plain);
    [[nodiscard]] static Result<QByteArray> decryptChunk(const QByteArray &key,
                                                         const QByteArray &nonceBase,
                                                         const QString &mediaId, int index,
                                                         const QByteArray &cipher);

    // BLAKE2b-256 of the plaintext, streaming so a 200 MiB video never sits in
    // memory. Returned as lowercase hex for the message descriptor.
    [[nodiscard]] static Result<QString> hashFile(const QString &path);
    [[nodiscard]] static Result<QString> hashBytes(const QByteArray &bytes);

    [[nodiscard]] static int chunkCount(qint64 plainSize);
    [[nodiscard]] static qint64 cipherSize(qint64 plainSize);
};

} // namespace mini::services
