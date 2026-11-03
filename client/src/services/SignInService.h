#pragma once

#include "core/Result.h"
#include "crypto/SignInKey.h"

#include <QString>

namespace mini::services {

// Creates, reads and stores sign.in. Writes to Downloads by default and falls
// back to data/ when that directory is missing or read-only (story 27).
class SignInService
{
public:
    explicit SignInService(QString downloadsDirectory = QString());

    [[nodiscard]] static QString defaultDownloadsDirectory();

    // Generates a fresh 20-token file; nothing is written yet.
    [[nodiscard]] Result<crypto::SignInMaterial> create() const;

    // Parses and verifies a file (CHECK included). Missing/unreadable files
    // produce signin.missing.
    [[nodiscard]] Result<crypto::NormalizedSignIn> read(const QString &path) const;

    // Saves the file, choosing "sign.in", "sign (2).in", ... so an existing
    // file is never overwritten. `usedFallback` tells the UI that Downloads
    // was unavailable and the file landed in data/.
    [[nodiscard]] Result<QString> save(const crypto::SignInMaterial &material,
                                       bool *usedFallback = nullptr) const;

    // Saves under an exact path (used for the temporary file during reissue).
    [[nodiscard]] Result<QString> saveTo(const crypto::SignInMaterial &material,
                                         const QString &path) const;

    // Atomically replaces `path` with `temporaryPath`.
    [[nodiscard]] Result<QString> replaceFile(const QString &temporaryPath,
                                              const QString &path) const;

    // Atomic raw-byte write, used to restore the previous sign.in after a
    // failed reissue. Nothing is replaced when the write cannot commit.
    [[nodiscard]] Result<QString> writeBytes(const QString &path,
                                             const QByteArray &bytes) const;

    [[nodiscard]] static QString uniquePath(const QString &directory,
                                            const QString &baseName = QStringLiteral("sign.in"));

private:
    QString m_downloads;
};

} // namespace mini::services
