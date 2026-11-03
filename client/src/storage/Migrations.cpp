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
    // Groups (phase 3). Server group state is cached locally so the chat list
    // survives a restart; sender key material is sealed with BodyCipher.
    {4, R"sql(
CREATE TABLE IF NOT EXISTS groups (
    group_id        TEXT PRIMARY KEY,
    title           TEXT NOT NULL DEFAULT '',
    about           TEXT NOT NULL DEFAULT '',
    avatar_media    TEXT NOT NULL DEFAULT '',
    owner_id        TEXT NOT NULL DEFAULT '',
    is_public       INTEGER NOT NULL DEFAULT 0,
    slow_mode_sec   INTEGER NOT NULL DEFAULT 0,
    join_by_request INTEGER NOT NULL DEFAULT 0,
    invite_token    TEXT NOT NULL DEFAULT '',
    invite_link     TEXT NOT NULL DEFAULT '',
    my_role         TEXT NOT NULL DEFAULT 'member',
    my_permissions  INTEGER NOT NULL DEFAULT 0,
    member_count    INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_groups_updated ON groups(updated_at DESC);

CREATE TABLE IF NOT EXISTS group_members (
    group_id     TEXT NOT NULL REFERENCES groups(group_id) ON DELETE CASCADE,
    user_id      TEXT NOT NULL,
    login        TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL DEFAULT '',
    role         TEXT NOT NULL DEFAULT 'member',
    permissions  INTEGER NOT NULL DEFAULT 0,
    joined_at    INTEGER NOT NULL DEFAULT 0,
    muted_until  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(group_id, user_id)
);
CREATE INDEX IF NOT EXISTS ix_group_members_login ON group_members(group_id, login);

CREATE TABLE IF NOT EXISTS group_sender_keys (
    group_id        TEXT NOT NULL,
    sender_id       TEXT NOT NULL,
    key_id          INTEGER NOT NULL DEFAULT 0,
    iteration       INTEGER NOT NULL DEFAULT 0,
    prev_iteration  INTEGER NOT NULL DEFAULT 0,
    chain_key       BLOB NOT NULL,
    signing_public  BLOB NOT NULL,
    signing_private BLOB NOT NULL DEFAULT x'',
    updated_at      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(group_id, sender_id)
);
)sql"},
    // Channels (phase 3, §43). A channel is a group with publisher roles:
    // the same sender-key material, keyed by channel_id.
    {5, R"sql(
CREATE TABLE IF NOT EXISTS channels (
    channel_id       TEXT PRIMARY KEY,
    title            TEXT NOT NULL DEFAULT '',
    about            TEXT NOT NULL DEFAULT '',
    avatar_media     TEXT NOT NULL DEFAULT '',
    owner_id         TEXT NOT NULL DEFAULT '',
    is_public        INTEGER NOT NULL DEFAULT 0,
    linked_group     TEXT NOT NULL DEFAULT '',
    invite_token     TEXT NOT NULL DEFAULT '',
    invite_link      TEXT NOT NULL DEFAULT '',
    my_role          TEXT NOT NULL DEFAULT '',
    is_member        INTEGER NOT NULL DEFAULT 0,
    my_permissions   INTEGER NOT NULL DEFAULT 0,
    subscriber_count INTEGER NOT NULL DEFAULT 0,
    created_at       INTEGER NOT NULL DEFAULT 0,
    updated_at       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_channels_updated ON channels(updated_at DESC);

CREATE TABLE IF NOT EXISTS channel_members (
    channel_id   TEXT NOT NULL REFERENCES channels(channel_id) ON DELETE CASCADE,
    user_id      TEXT NOT NULL,
    login        TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL DEFAULT '',
    role         TEXT NOT NULL DEFAULT 'subscriber',
    permissions  INTEGER NOT NULL DEFAULT 0,
    joined_at    INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(channel_id, user_id)
);
CREATE INDEX IF NOT EXISTS ix_channel_members_login ON channel_members(channel_id, login);

CREATE TABLE IF NOT EXISTS channel_sender_keys (
    channel_id      TEXT NOT NULL,
    sender_id       TEXT NOT NULL,
    key_id          INTEGER NOT NULL DEFAULT 0,
    iteration       INTEGER NOT NULL DEFAULT 0,
    prev_iteration  INTEGER NOT NULL DEFAULT 0,
    chain_key       BLOB NOT NULL,
    signing_public  BLOB NOT NULL,
    signing_private BLOB NOT NULL DEFAULT x'',
    updated_at      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(channel_id, sender_id)
);
)sql"},
    // Media (phase 3, §40). The message row keeps the caption in `body` and
    // points at one media row; the file key and nonce base are sealed with
    // DB_KEY, so a copied database alone cannot decrypt downloaded chunks.
    {6, R"sql(
CREATE TABLE IF NOT EXISTS media (
    media_id          TEXT PRIMARY KEY,
    chat_id           TEXT NOT NULL DEFAULT '',
    kind              TEXT NOT NULL DEFAULT 'file',
    mime              TEXT NOT NULL DEFAULT '',
    name              TEXT NOT NULL DEFAULT '',
    size              INTEGER NOT NULL DEFAULT 0,
    chunk_size        INTEGER NOT NULL DEFAULT 0,
    file_hash         TEXT NOT NULL DEFAULT '',
    file_key          BLOB NOT NULL DEFAULT x'',
    nonce_base        BLOB NOT NULL DEFAULT x'',
    width             INTEGER NOT NULL DEFAULT 0,
    height            INTEGER NOT NULL DEFAULT 0,
    duration_ms       INTEGER NOT NULL DEFAULT 0,
    state             TEXT NOT NULL DEFAULT 'prepared',
    uploaded_chunks   INTEGER NOT NULL DEFAULT 0,
    downloaded_chunks INTEGER NOT NULL DEFAULT 0,
    local_path        TEXT NOT NULL DEFAULT '',
    created_at        INTEGER NOT NULL DEFAULT 0,
    updated_at        INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_media_chat ON media(chat_id, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_media_hash ON media(chat_id, file_hash);

CREATE TABLE IF NOT EXISTS media_meta (
    media_id TEXT NOT NULL,
    key      TEXT NOT NULL,
    value    TEXT NOT NULL,
    PRIMARY KEY(media_id, key)
);
)sql"},
    // Phase 3 message actions (§37, §38.2) and chat archive/mute (§32.4).
    // Columns are added by ensureActionColumns so an interrupted migration is
    // safe to re-run.
    {7, R"sql(
CREATE TABLE IF NOT EXISTS chat_pins (
    chat_id       TEXT NOT NULL,
    client_msg_id TEXT NOT NULL REFERENCES messages(client_msg_id) ON DELETE CASCADE,
    pinned_at     INTEGER NOT NULL DEFAULT 0,
    pinned_by     TEXT NOT NULL DEFAULT '',
    PRIMARY KEY(chat_id, client_msg_id)
);
CREATE INDEX IF NOT EXISTS ix_chat_pins_order ON chat_pins(chat_id, pinned_at DESC);
)sql"},
};

// SQLite has no ADD COLUMN IF NOT EXISTS, and a database may already have the
// media columns when schema_version was rewound by a test or a crash. Check
// PRAGMA first so migration 6 is idempotent.
bool messageColumnExists(Database &db, const char *column)
{
    detail::Statement statement(db.handle(), "PRAGMA table_info(messages)");
    if (!statement.isOk()) {
        return true; // cannot inspect: do not attempt a blind ALTER
    }
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        if (statement.text(1) == QLatin1String(column)) {
            return true;
        }
        rc = statement.step();
    }
    return false;
}

Result<bool> ensureMessageMediaColumns(Database &db)
{
    struct Column
    {
        const char *name;
        const char *sql;
    };
    static const Column columns[] = {
        {"kind", "ALTER TABLE messages ADD COLUMN kind TEXT NOT NULL DEFAULT 'text'"},
        {"media_id", "ALTER TABLE messages ADD COLUMN media_id TEXT NOT NULL DEFAULT ''"},
        {"media_name", "ALTER TABLE messages ADD COLUMN media_name TEXT NOT NULL DEFAULT ''"},
        {"media_size", "ALTER TABLE messages ADD COLUMN media_size INTEGER NOT NULL DEFAULT 0"},
        {"media_duration_ms",
         "ALTER TABLE messages ADD COLUMN media_duration_ms INTEGER NOT NULL DEFAULT 0"},
    };
    for (const Column &column : columns) {
        if (messageColumnExists(db, column.name)) {
            continue;
        }
        if (auto added = db.exec(QByteArray(column.sql)); added.isError()) {
            return added;
        }
    }
    return Result<bool>::ok(true);
}

// Phase 3 additions. A database rewound by a test may already have some of
// these columns, so PRAGMA decides per column instead of failing the ALTER.
Result<bool> ensureActionColumns(Database &db)
{
    struct Column
    {
        const char *table;
        const char *name;
        const char *sql;
    };
    static const Column columns[] = {
        {"messages", "edited_at",
         "ALTER TABLE messages ADD COLUMN edited_at INTEGER NOT NULL DEFAULT 0"},
        {"messages", "forwarded_from",
         "ALTER TABLE messages ADD COLUMN forwarded_from TEXT NOT NULL DEFAULT ''"},
        {"chats", "muted_until",
         "ALTER TABLE chats ADD COLUMN muted_until INTEGER NOT NULL DEFAULT 0"},
        {"chats", "archived", "ALTER TABLE chats ADD COLUMN archived INTEGER NOT NULL DEFAULT 0"},
    };
    for (const Column &column : columns) {
        detail::Statement info(db.handle(),
                               QStringLiteral("PRAGMA table_info(%1)")
                                   .arg(QString::fromLatin1(column.table))
                                   .toUtf8()
                                   .constData());
        if (!info.isOk()) {
            continue;
        }
        bool exists = false;
        int rc = info.step();
        while (rc == SQLITE_ROW) {
            if (info.text(1) == QLatin1String(column.name)) {
                exists = true;
                break;
            }
            rc = info.step();
        }
        if (exists) {
            continue;
        }
        if (auto added = db.exec(QByteArray(column.sql)); added.isError()) {
            return added;
        }
    }
    return Result<bool>::ok(true);
}

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
            if (targetVersion >= 6) {
                if (auto columns = ensureMessageMediaColumns(tx); columns.isError()) {
                    return columns;
                }
            }
            if (targetVersion >= 7) {
                if (auto columns = ensureActionColumns(tx); columns.isError()) {
                    return columns;
                }
            }
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
