#pragma once

#include "core/Result.h"
#include "domain/Chat.h"
#include "domain/Message.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <utility>

namespace mini::services::backup {

// §64.3: the on-disk container is MAGIC(8) || VER(1) || NONCE(24) || CT.
inline constexpr int kVersion = 1;
inline constexpr char kMagic[8] = {'M', 'I', 'N', 'I', 'B', 'K', '0', '1'};

// §49.1: K_export = HKDF(K_mac, info="export/v1"). An empty salt matches the
// other HKDF branches in Kdf.
[[nodiscard]] QByteArray deriveExportKey(const QByteArray &kMac);

// Adds the fixed header and seals the zip bytes with XChaCha20-Poly1305.
// AAD is MAGIC || VER, so the header cannot be swapped without failing.
[[nodiscard]] Result<QByteArray> sealArchive(const QByteArray &zip,
                                             const QByteArray &key);
// Checks the magic and version before touching the ciphertext.
[[nodiscard]] Result<QByteArray> openArchive(const QByteArray &file,
                                             const QByteArray &key);

// In-memory helpers. The archive is small enough for phase 4; every caller
// still enforces a size cap before allocating.
[[nodiscard]] Result<QByteArray>
buildZip(const QVector<std::pair<QString, QByteArray>> &entries);
[[nodiscard]] Result<QHash<QString, QByteArray>> readZip(const QByteArray &zip);

[[nodiscard]] QJsonObject messageToJson(const Message &message,
                                        const QString &mediaExtension = QString());
// mediaExtension is read back so import can locate media/<id>.<ext>.
[[nodiscard]] bool messageFromJson(const QJsonObject &object, Message *message,
                                   QString *mediaExtension);

[[nodiscard]] QJsonObject chatToJson(const Chat &chat);
[[nodiscard]] bool chatFromJson(const QJsonObject &object, Chat *chat);

// Archive entry names must not escape media/ even though import looks them up
// by a constructed key.
[[nodiscard]] bool safeMediaId(const QString &mediaId);
[[nodiscard]] QString safeMediaExtension(const QString &extension);

} // namespace mini::services::backup
