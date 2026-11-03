#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace mini::crypto {

// Independently derived keys. Only K_auth and PW_KEY are ever sent to the
// server (inside TLS); K_enc/K_mac stay on the device.
struct FileKeys
{
    QByteArray kfile;
    QByteArray kauth;
    QByteArray kenc;
    QByteArray kmac;
};

// Key derivation fixed by spec §4.3:
//   K_file = Argon2id(normalized, salt_file)
//   K_*    = HKDF-SHA256(K_file, info="MINI/auth/v1" etc.)
//   PW_KEY = Argon2id(password, salt_pw)
class Kdf
{
public:
    Kdf() = delete;

    static constexpr int kKeyBytes = 32;
    static constexpr int kSaltBytes = 16;
    static constexpr quint64 kMemoryBytes = 64ull * 1024ull * 1024ull;
    static constexpr quint64 kOpsLimit = 3;
    static constexpr int kParallelism = 1;

    [[nodiscard]] static Result<FileKeys> deriveFileKeys(const QString &normalized,
                                                         const QByteArray &saltFile);
    [[nodiscard]] static Result<QByteArray> derivePasswordKey(const QString &password,
                                                              const QByteArray &saltPw);
    [[nodiscard]] static Result<QByteArray> deriveDbKey(const QByteArray &kenc);
    [[nodiscard]] static Result<QByteArray> deriveKeyWrap(const QByteArray &kenc);

    // RFC 5869 HKDF-SHA256. Exposed for tests and for storage key branches.
    [[nodiscard]] static QByteArray hkdfSha256(const QByteArray &ikm,
                                               const QByteArray &salt,
                                               const QByteArray &info,
                                               int length = kKeyBytes);
};

} // namespace mini::crypto
