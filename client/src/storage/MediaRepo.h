#pragma once

#include "core/Result.h"
#include "domain/Media.h"
#include "storage/BodyCipher.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

// Media rows live beside messages: the message row carries the caption and a
// media_id, the media row carries the sealed file key, sizes and progress.
// Must only be called on the StorageWorker thread.
class MediaRepo
{
public:
    explicit MediaRepo(Database &db);

    Result<bool> insert(const MediaRecord &record);
    Result<std::optional<MediaRecord>> get(const QString &mediaId) const;
    // Resume seam: an interrupted upload of the same plaintext in this chat.
    Result<std::optional<MediaRecord>> findPending(const QString &chatId,
                                                   const QString &fileHash) const;
    Result<bool> updateState(const QString &mediaId, const QString &state,
                             int uploadedChunks = -1, int downloadedChunks = -1,
                             const QString &localPath = QString());
    // Drops a staging row whose server upload expired, so the next attempt
    // starts a fresh upload instead of replaying a dead id.
    Result<bool> remove(const QString &mediaId);

    Result<QString> meta(const QString &mediaId, const QString &key) const;
    Result<bool> setMeta(const QString &mediaId, const QString &key,
                         const QString &value);

private:
    Database &m_db;
    BodyCipher m_cipher;
};

} // namespace mini::storage
