#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mini::crypto {

// A parsed and verified sign.in file. `normalized` is the canonical,
// whitespace-insensitive 20-token string; `check` is the base32 BLAKE2b-64
// checksum of that string.
struct NormalizedSignIn
{
    QStringList tokens;
    QString normalized;
    QString check;
};

struct SignInMaterial
{
    NormalizedSignIn parsed;
    QByteArray fileBytes;
};

// Generates and validates the MINI-SIGNIN-1 recovery file:
//   5 words from a 1024-word list, 10 letters, 5 symbols, then CHECK.
// Normalization tolerates BOM, CRLF and extra whitespace; the token text is
// compared case-sensitively, so editing any token breaks CHECK.
class SignInKey
{
public:
    SignInKey() = delete;

    // 20 CSPRNG tokens + checksum + the exact bytes to write to disk.
    [[nodiscard]] static SignInMaterial generate();

    // Parses file contents. Returns an error for a bad header, token count,
    // token alphabet, malformed CHECK or a checksum mismatch.
    [[nodiscard]] static Result<NormalizedSignIn> normalize(const QByteArray &fileContents);

    // CHECK value for an already normalized token string.
    [[nodiscard]] static QString checksumFor(const QString &normalized);

    // Constant-time comparison of the stored and expected checksum.
    [[nodiscard]] static bool verifyChecksum(const NormalizedSignIn &parsed);

    [[nodiscard]] static bool isWordToken(const QString &token);
    [[nodiscard]] static bool isLetterToken(const QString &token);
    [[nodiscard]] static bool isSymbolToken(const QString &token);

    [[nodiscard]] static int wordCount();
};

} // namespace mini::crypto
