#include "storage/Database.h"

#include "storage/Backup.h"
#include "storage/Migrations.h"
#include "storage/Sql.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <sodium.h>

namespace mini::storage {
namespace {

// A corrupted file is set aside instead of deleted so a user can still try to
// salvage it by hand.
QString quarantinePath(const QString &path)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return path + QStringLiteral(".corrupt-") + stamp;
}

void removeSidecars(const QString &path)
{
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

} // namespace

Database::Database(QString path, QByteArray dbKey)
    : m_path(std::move(path)), m_dbKey(std::move(dbKey))
{
}

Database::~Database()
{
    close();
}

Result<std::unique_ptr<Database>> Database::open(const QString &path, const QByteArray &dbKey,
                                                 const QString &backupsDir)
{
    if (dbKey.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        return Result<std::unique_ptr<Database>>::error(
            QStringLiteral("DB_KEY must be %1 bytes")
                .arg(crypto_aead_xchacha20poly1305_ietf_KEYBYTES));
    }
    if (sodium_init() < 0) {
        return Result<std::unique_ptr<Database>>::error(
            QStringLiteral("libsodium initialisation failed"));
    }

    auto database = std::unique_ptr<Database>(new Database(path, dbKey));

    QString openError;
    QString integrityError;
    if (database->openConnection(&openError) && database->checkIntegrity(&integrityError)) {
        if (auto migrated = Migrations::apply(*database, backupsDir); migrated.isError()) {
            return Result<std::unique_ptr<Database>>::error(migrated.error());
        }
        return Result<std::unique_ptr<Database>>::ok(std::move(database));
    }

    const QString failure = !openError.isEmpty() ? openError : integrityError;
    database->close();

    QString recoveryMessage;
    if (!database->recoverFromBackup(backupsDir, &recoveryMessage, &openError)) {
        return Result<std::unique_ptr<Database>>::error(
            QStringLiteral("database is unusable (%1); backup restore failed: %2")
                .arg(failure, openError));
    }

    if (auto migrated = Migrations::apply(*database, backupsDir); migrated.isError()) {
        return Result<std::unique_ptr<Database>>::error(migrated.error());
    }

    database->m_recovered = true;
    database->m_recoveryMessage = recoveryMessage;
    return Result<std::unique_ptr<Database>>::ok(std::move(database));
}

bool Database::openConnection(QString *error)
{
    sqlite3 *handle = nullptr;
    const QByteArray utf8Path = m_path.toUtf8();
    const int rc = sqlite3_open_v2(utf8Path.constData(), &handle,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = handle ? QString::fromUtf8(sqlite3_errmsg(handle))
                            : QStringLiteral("cannot open database");
        }
        if (handle) {
            sqlite3_close_v2(handle);
        }
        return false;
    }
    m_db = handle;

    static const char *pragmas = R"sql(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA foreign_keys=ON;
PRAGMA busy_timeout=5000;
PRAGMA temp_store=MEMORY;
PRAGMA secure_delete=ON;
)sql";
    if (!exec(QByteArray(pragmas)).isOk()) {
        if (error) {
            *error = detail::sqlError(m_db, "pragmas");
        }
        close();
        return false;
    }
    return true;
}

bool Database::checkIntegrity(QString *error)
{
    if (!m_db) {
        if (error) {
            *error = QStringLiteral("database is not open");
        }
        return false;
    }
    detail::Statement statement(m_db, "PRAGMA quick_check");
    if (!statement.isOk()) {
        if (error) {
            *error = detail::sqlError(m_db, "quick_check");
        }
        return false;
    }
    const int rc = statement.step();
    if (rc != SQLITE_ROW || statement.text(0) != QStringLiteral("ok")) {
        if (error) {
            *error = rc == SQLITE_ROW ? statement.text(0)
                                      : detail::sqlError(m_db, "quick_check");
        }
        return false;
    }
    return true;
}

bool Database::recoverFromBackup(const QString &backupsDir, QString *recoveryMessage, QString *error)
{
    if (backupsDir.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no backup directory configured");
        }
        return false;
    }
    const auto backup = Backup::newest(backupsDir);
    if (!backup.has_value()) {
        if (error) {
            *error = QStringLiteral("no backup found");
        }
        return false;
    }

    const QString corruptPath = quarantinePath(m_path);
    if (!QFile::rename(m_path, corruptPath)) {
        // The corrupt file must not stay in the way of the restored copy.
        if (!QFile::remove(m_path)) {
            if (error) {
                *error = QStringLiteral("cannot replace the corrupted file");
            }
            return false;
        }
    }
    removeSidecars(m_path);

    if (!QFile::copy(backup->databasePath, m_path)) {
        if (error) {
            *error = QStringLiteral("cannot copy the backup into place");
        }
        return false;
    }

    QString reopenError;
    if (!openConnection(&reopenError) || !checkIntegrity(&reopenError)) {
        if (error) {
            *error = reopenError;
        }
        return false;
    }

    if (recoveryMessage) {
        *recoveryMessage = QStringLiteral("database was corrupted; restored %1")
                               .arg(QFileInfo(backup->directory).fileName());
    }
    return true;
}

int Database::schemaVersion() const
{
    if (!m_db) {
        return 0;
    }
    auto version = Migrations::currentVersion(const_cast<Database &>(*this));
    return version.isOk() ? version.value() : 0;
}

Result<bool> Database::exec(const QByteArray &sql)
{
    if (!m_db) {
        return Result<bool>::error(QStringLiteral("database is not open"));
    }
    char *message = nullptr;
    const int rc = sqlite3_exec(m_db, sql.constData(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        const QString text = message ? QString::fromUtf8(message) : detail::sqlError(m_db, "exec");
        sqlite3_free(message);
        return Result<bool>::error(text);
    }
    return Result<bool>::ok(true);
}

Result<bool> Database::transaction(const std::function<Result<bool>(Database &)> &fn)
{
    if (!m_db) {
        return Result<bool>::error(QStringLiteral("database is not open"));
    }
    if (auto begun = exec(QByteArrayLiteral("BEGIN IMMEDIATE")); begun.isError()) {
        return begun;
    }
    auto result = fn(*this);
    if (result.isError()) {
        exec(QByteArrayLiteral("ROLLBACK"));
        return result;
    }
    if (auto committed = exec(QByteArrayLiteral("COMMIT")); committed.isError()) {
        exec(QByteArrayLiteral("ROLLBACK"));
        return committed;
    }
    return Result<bool>::ok(true);
}

void Database::close()
{
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

} // namespace mini::storage
