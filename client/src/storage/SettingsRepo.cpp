#include "storage/SettingsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

namespace mini::storage {

SettingsRepo::SettingsRepo(Database &db) : m_db(db)
{
}

Result<QString> SettingsRepo::get(const QString &key, const QString &fallback) const
{
    detail::Statement statement(m_db.handle(), "SELECT value FROM settings WHERE key = ?");
    if (!statement.isOk()) {
        return Result<QString>::error(detail::sqlError(m_db.handle(), "settings.get"));
    }
    statement.bindText(1, key);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<QString>::ok(statement.text(0));
    }
    if (rc != SQLITE_DONE) {
        return Result<QString>::error(detail::sqlError(m_db.handle(), "settings.get"));
    }
    return Result<QString>::ok(fallback);
}

Result<bool> SettingsRepo::set(const QString &key, const QString &value)
{
    if (key.isEmpty()) {
        return Result<bool>::error(QStringLiteral("settings key is required"));
    }
    detail::Statement statement(
        m_db.handle(),
        "INSERT INTO settings(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "settings.set"));
    }
    statement.bindText(1, key);
    statement.bindText(2, value);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "settings.set"));
    }
    return Result<bool>::ok(true);
}

Result<bool> SettingsRepo::remove(const QString &key)
{
    detail::Statement statement(m_db.handle(), "DELETE FROM settings WHERE key = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "settings.remove"));
    }
    statement.bindText(1, key);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "settings.remove"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

} // namespace mini::storage
