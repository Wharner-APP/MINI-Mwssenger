#include "storage/MediaRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "media_id, chat_id, kind, mime, name, size, chunk_size, file_hash, file_key, "
    "nonce_base, width, height, duration_ms, state, uploaded_chunks, "
    "downloaded_chunks, local_path, created_at, updated_at";

QByteArray keyAad(const QString &mediaId)
{
    return QByteArrayLiteral("media-key:") + mediaId.toUtf8();
}

QByteArray nonceAad(const QString &mediaId)
{
    return QByteArrayLiteral("media-nonce:") + mediaId.toUtf8();
}

QByteArray sealBytes(const BodyCipher &cipher, const QString &mediaId,
                     const QByteArray &plain, const QByteArray &aad)
{
    if (plain.isEmpty() || !cipher.isValid()) {
        return {};
    }
    const auto sealed = cipher.encrypt(QString::fromLatin1(plain.toBase64()), aad);
    Q_UNUSED(mediaId);
    return sealed.isOk() ? sealed.value() : QByteArray();
}

QByteArray openBytes(const BodyCipher &cipher, const QByteArray &sealed,
                     const QByteArray &aad)
{
    if (sealed.isEmpty() || !cipher.isValid()) {
        return {};
    }
    const auto plain = cipher.decrypt(sealed, aad);
    if (plain.isError()) {
        return {};
    }
    return QByteArray::fromBase64(plain.value().toLatin1());
}

MediaRecord readRecord(const detail::Statement &statement)
{
    MediaRecord record;
    record.mediaId = statement.text(0);
    record.chatId = statement.text(1);
    record.kind = mediaKindFromString(statement.text(2));
    record.mime = statement.text(3);
    record.name = statement.text(4);
    record.size = statement.int64(5);
    record.chunkSize = statement.int64(6);
    record.fileHash = statement.text(7);
    record.width = statement.integer(10);
    record.height = statement.integer(11);
    record.durationMs = statement.int64(12);
    record.state = statement.text(13);
    record.uploadedChunks = statement.integer(14);
    record.downloadedChunks = statement.integer(15);
    record.localPath = statement.text(16);
    record.createdAt = statement.int64(17);
    record.updatedAt = statement.int64(18);
    return record;
}

} // namespace

MediaRepo::MediaRepo(Database &db) : m_db(db), m_cipher(db.dbKey()) {}

Result<bool> MediaRepo::insert(const MediaRecord &record)
{
    if (record.mediaId.isEmpty()) {
        return Result<bool>::error(QStringLiteral("media_id is required"));
    }
    const qint64 now = record.createdAt > 0 ? record.createdAt
                                            : QDateTime::currentMSecsSinceEpoch();
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT OR IGNORE INTO media(
                media_id, chat_id, kind, mime, name, size, chunk_size, file_hash,
                file_key, nonce_base, width, height, duration_ms, state,
                uploaded_chunks, downloaded_chunks, local_path, created_at, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.insert"));
    }
    statement.bindText(1, record.mediaId);
    statement.bindText(2, record.chatId);
    statement.bindText(3, mediaKindToString(record.kind));
    statement.bindText(4, record.mime);
    statement.bindText(5, record.name);
    statement.bindInt64(6, record.size);
    statement.bindInt64(7, record.chunkSize);
    statement.bindText(8, record.fileHash);
    statement.bindBlob(9, sealBytes(m_cipher, record.mediaId, record.fileKey,
                                    keyAad(record.mediaId)));
    statement.bindBlob(10, sealBytes(m_cipher, record.mediaId, record.nonceBase,
                                     nonceAad(record.mediaId)));
    statement.bindInt(11, record.width);
    statement.bindInt(12, record.height);
    statement.bindInt64(13, record.durationMs);
    statement.bindText(14, record.state.isEmpty() ? QStringLiteral("prepared")
                                                  : record.state);
    statement.bindInt(15, record.uploadedChunks);
    statement.bindInt(16, record.downloadedChunks);
    statement.bindText(17, record.localPath);
    statement.bindInt64(18, now);
    statement.bindInt64(19, now);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.insert"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<std::optional<MediaRecord>> MediaRepo::get(const QString &mediaId) const
{
    detail::Statement statement(
        m_db.handle(), QStringLiteral("SELECT %1 FROM media WHERE media_id = ?")
                           .arg(QString::fromLatin1(kColumns))
                           .toUtf8()
                           .constData());
    if (!statement.isOk()) {
        return Result<std::optional<MediaRecord>>::error(
            detail::sqlError(m_db.handle(), "media.get"));
    }
    statement.bindText(1, mediaId);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<std::optional<MediaRecord>>::ok(std::nullopt);
    }
    if (rc != SQLITE_ROW) {
        return Result<std::optional<MediaRecord>>::error(
            detail::sqlError(m_db.handle(), "media.get"));
    }
    MediaRecord record = readRecord(statement);
    record.fileKey = openBytes(m_cipher, statement.blob(8), keyAad(record.mediaId));
    record.nonceBase = openBytes(m_cipher, statement.blob(9), nonceAad(record.mediaId));
    return Result<std::optional<MediaRecord>>::ok(record);
}

Result<std::optional<MediaRecord>> MediaRepo::findPending(const QString &chatId,
                                                          const QString &fileHash) const
{
    if (fileHash.isEmpty()) {
        return Result<std::optional<MediaRecord>>::ok(std::nullopt);
    }
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM media WHERE chat_id = ? AND file_hash = ? "
                       "AND state IN ('prepared', 'uploading', 'failed') "
                       "ORDER BY created_at DESC LIMIT 1")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<MediaRecord>>::error(
            detail::sqlError(m_db.handle(), "media.findPending"));
    }
    statement.bindText(1, chatId);
    statement.bindText(2, fileHash);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<std::optional<MediaRecord>>::ok(std::nullopt);
    }
    if (rc != SQLITE_ROW) {
        return Result<std::optional<MediaRecord>>::error(
            detail::sqlError(m_db.handle(), "media.findPending"));
    }
    MediaRecord record = readRecord(statement);
    record.fileKey = openBytes(m_cipher, statement.blob(8), keyAad(record.mediaId));
    record.nonceBase = openBytes(m_cipher, statement.blob(9), nonceAad(record.mediaId));
    return Result<std::optional<MediaRecord>>::ok(record);
}

Result<bool> MediaRepo::updateState(const QString &mediaId, const QString &state,
                                    int uploadedChunks, int downloadedChunks,
                                    const QString &localPath)
{
    detail::Statement statement(
        m_db.handle(),
        R"sql(UPDATE media SET
                state = ?,
                uploaded_chunks = CASE WHEN ? >= 0 THEN ? ELSE uploaded_chunks END,
                downloaded_chunks = CASE WHEN ? >= 0 THEN ? ELSE downloaded_chunks END,
                local_path = CASE WHEN ? <> '' THEN ? ELSE local_path END,
                updated_at = ?
              WHERE media_id = ?)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.updateState"));
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    statement.bindText(1, state);
    statement.bindInt(2, uploadedChunks);
    statement.bindInt(3, uploadedChunks);
    statement.bindInt(4, downloadedChunks);
    statement.bindInt(5, downloadedChunks);
    statement.bindText(6, localPath);
    statement.bindText(7, localPath);
    statement.bindInt64(8, now);
    statement.bindText(9, mediaId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.updateState"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> MediaRepo::remove(const QString &mediaId)
{
    detail::Statement meta(m_db.handle(),
                           "DELETE FROM media_meta WHERE media_id = ?");
    if (!meta.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.remove.meta"));
    }
    meta.bindText(1, mediaId);
    if (meta.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.remove.meta"));
    }
    detail::Statement row(m_db.handle(), "DELETE FROM media WHERE media_id = ?");
    if (!row.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.remove"));
    }
    row.bindText(1, mediaId);
    if (row.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.remove"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<QString> MediaRepo::meta(const QString &mediaId, const QString &key) const
{
    detail::Statement statement(
        m_db.handle(),
        "SELECT value FROM media_meta WHERE media_id = ? AND key = ?");
    if (!statement.isOk()) {
        return Result<QString>::error(detail::sqlError(m_db.handle(), "media.meta"));
    }
    statement.bindText(1, mediaId);
    statement.bindText(2, key);
    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return Result<QString>::ok(QString());
    }
    if (rc != SQLITE_ROW) {
        return Result<QString>::error(detail::sqlError(m_db.handle(), "media.meta"));
    }
    return Result<QString>::ok(statement.text(0));
}

Result<bool> MediaRepo::setMeta(const QString &mediaId, const QString &key,
                                const QString &value)
{
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO media_meta(media_id, key, value) VALUES(?, ?, ?)
              ON CONFLICT(media_id, key) DO UPDATE SET value = excluded.value)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.setMeta"));
    }
    statement.bindText(1, mediaId);
    statement.bindText(2, key);
    statement.bindText(3, value);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "media.setMeta"));
    }
    return Result<bool>::ok(true);
}

} // namespace mini::storage
