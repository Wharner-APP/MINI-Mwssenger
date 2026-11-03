#include "storage/Migrations.h"

#include "storage/Backup.h"
#include "storage/BodyCipher.h"
#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVector>

namespace mini::storage {
namespace {

struct Migration
{
    int version;
    const char *sql;
};

// Keep migrations append-only. Version 1 creates the whole first-build schema.
constexpr Migration kMigrations[] = {
    {1, R"sql(
CREATE TABLE chats (
    chat_id              TEXT PRIMARY KEY,
    kind                 TEXT NOT NULL DEFAULT 'direct',
    peer_login           TEXT NOT NULL DEFAULT '',
    title                TEXT NOT NULL DEFAULT '',
    last_message_ts      INTEGER NOT NULL DEFAULT 0,
    last_message_preview TEXT NOT NULL DEFAULT '',
    unread_count         INTEGER NOT NULL DEFAULT 0,
    pinned               INTEGER NOT NULL DEFAULT 0,
    muted                INTEGER NOT NULL DEFAULT 0,
    updated_at           INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX ix_chats_order ON chats(pinned DESC, last_message_ts DESC);

CREATE TABLE messages (
    client_msg_id  TEXT PRIMARY KEY,
    chat_id        TEXT NOT NULL REFERENCES chats(chat_id) ON DELETE CASCADE,
    server_msg_id  TEXT,
    sender_login   TEXT NOT NULL DEFAULT '',
    body           BLOB NOT NULL,
    state          TEXT NOT NULL,
    outgoing       INTEGER NOT NULL DEFAULT 1,
    local_ts       INTEGER NOT NULL DEFAULT 0,
    server_ts      INTEGER NOT NULL DEFAULT 0,
    reply_to       TEXT
);
CREATE UNIQUE INDEX ux_messages_server
    ON messages(chat_id, server_msg_id) WHERE server_msg_id IS NOT NULL;
CREATE INDEX ix_messages_history ON messages(chat_id, server_ts, local_ts, client_msg_id);

CREATE TABLE contacts (
    login           TEXT PRIMARY KEY,
    display_name    TEXT NOT NULL DEFAULT '',
    safety_verified INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE outbox (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id         TEXT NOT NULL,
    client_msg_id   TEXT NOT NULL UNIQUE,
    attempts        INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL DEFAULT 0,
    next_attempt_at INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX ix_outbox_chat ON outbox(chat_id, id);

CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)sql"},
    // The FTS index used to live in the database file, which left plaintext
    // tokens on disk. It is now a TEMP (in-memory) table rebuilt at startup.
    {2, R"sql(
DROP TABLE IF EXISTS messages_fts;
)sql"},
    // v3 exists so databases already at v2 (before previews were re-sealed)
    // run the same preview encryption and VACUUM once more.
    {3, R"sql(
SELECT 1;
)sql"},
};

// v1 stored the chat preview as plaintext. Seal every legacy value that is
// not already an AEAD blob; a second run is a no-op.
Result<bool> encryptLegacyChatPreviews(Database &db)
{
    BodyCipher cipher(db.dbKey());
    if (!cipher.isValid()) {
        return Result<bool>::error(QStringLiteral("migration preview key"));
    }

    struct Row
    {
        QString chatId;
        QByteArray stored;
    };
    QVector<Row> rows;
    {
        detail::Statement select(db.handle(),
                                 "SELECT chat_id, last_message_preview FROM chats");
        if (!select.isOk()) {
            return Result<bool>::error(detail::sqlError(db.handle(), "migration previews"));
        }
        int rc = select.step();
        while (rc == SQLITE_ROW) {
            rows.push_back({select.text(0), select.blob(1)});
            rc = select.step();
        }
        if (rc != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(db.handle(), "migration previews"));
        }
    }

    detail::Statement update(db.handle(),
                             "UPDATE chats SET last_message_preview = ? WHERE chat_id = ?");
    if (!update.isOk()) {
        return Result<bool>::error(detail::sqlError(db.handle(), "migration previews"));
    }
    for (const Row &row : rows) {
        const QByteArray aad = chatPreviewAad(row.chatId);
        if (!row.stored.isEmpty() && cipher.decrypt(row.stored, aad).isOk()) {
            continue; // already sealed
        }
        const Result<QByteArray> sealed =
            cipher.encrypt(QString::fromUtf8(row.stored), aad);
        if (sealed.isError()) {
            return Result<bool>::error(sealed.error());
        }
        sqlite3_reset(update.get());
        sqlite3_clear_bindings(update.get());
        update.bindBlob(1, sealed.value());
        update.bindText(2, row.chatId);
        if (update.step() != SQLITE_DONE) {
            return Result<bool>::error(detail::sqlError(db.handle(), "migration previews"));
        }
    }
    return Result<bool>::ok(true);
}

} // namespace

Result<int> Migrations::currentVersion(Database &db)
{
    if (auto created = db.exec(QByteArrayLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"));
        created.isError()) {
        return Result<int>::error(created.error());
    }

    detail::Statement select(db.handle(), "SELECT version FROM schema_version LIMIT 1");
    if (!select.isOk()) {
        return Result<int>::error(detail::sqlError(db.handle(), "schema_version"));
    }
    const int rc = select.step();
    if (rc == SQLITE_ROW) {
        return Result<int>::ok(select.integer(0));
    }
    if (rc != SQLITE_DONE) {
        return Result<int>::error(detail::sqlError(db.handle(), "schema_version"));
    }
    if (auto inserted = db.exec(QByteArrayLiteral("INSERT INTO schema_version(version) VALUES(0)"));
        inserted.isError()) {
        return Result<int>::error(inserted.error());
    }
    return Result<int>::ok(0);
}

Result<bool> Migrations::apply(Database &db, const QString &backupsDir)
{
    auto version = currentVersion(db);
    if (version.isError()) {
        return Result<bool>::error(version.error());
    }
    int current = version.value();
    if (current > latestVersion) {
        return Result<bool>::error(
            QStringLiteral("database schema %1 is newer than this build (%2)")
                .arg(current)
                .arg(latestVersion));
    }
    if (current == latestVersion) {
        return Result<bool>::ok(true);
    }

    // Preserve the previous schema before touching an existing database.
    if (current > 0 && !backupsDir.isEmpty()) {
        if (!QDir().mkpath(backupsDir)) {
            return Result<bool>::error(
                QStringLiteral("cannot create the backup directory: %1").arg(backupsDir));
        }
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString target = QDir(backupsDir).filePath(
            QStringLiteral("pre-migration-v%1-%2.db").arg(current).arg(stamp));
        if (auto copied = copyDatabaseFile(db, target); copied.isError()) {
            return Result<bool>::error(
                QStringLiteral("pre-migration backup failed: %1").arg(copied.error()));
        }
    }

    bool vacuumNeeded = false;
    for (const Migration &migration : kMigrations) {
        if (migration.version <= current) {
            continue;
        }
        const int targetVersion = migration.version;
        auto result = db.transaction([&](Database &tx) {
            if (auto executed = tx.exec(QByteArray(migration.sql)); executed.isError()) {
                return executed;
            }
            if (targetVersion >= 2) {
                if (auto sealed = encryptLegacyChatPreviews(tx); sealed.isError()) {
                    return sealed;
                }
            }
            const QByteArray update =
                QByteArrayLiteral("UPDATE schema_version SET version = ")
                + QByteArray::number(targetVersion);
            return tx.exec(update);
        });
        if (result.isError()) {
            return Result<bool>::error(
                QStringLiteral("migration %1 failed: %2").arg(targetVersion).arg(result.error()));
        }
        if (targetVersion >= 2) {
            vacuumNeeded = true;
        }
        current = targetVersion;
    }

    if (vacuumNeeded) {
        // secure_delete wipes the dropped FTS pages; VACUUM rebuilds the file
        // so no plaintext token survives in the freelist.
        if (auto vacuum = db.exec(QByteArrayLiteral("VACUUM")); vacuum.isError()) {
            return Result<bool>::error(
                QStringLiteral("migration vacuum failed: %1").arg(vacuum.error()));
        }
    }

    return Result<bool>::ok(true);
}

} // namespace mini::storage
