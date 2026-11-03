#pragma once

#include "core/Result.h"

#include <QString>

namespace mini::storage {

class Database;

// Account-scoped key/value pairs stored in SQLite (not config/settings.ini).
class SettingsRepo
{
public:
    explicit SettingsRepo(Database &db);

    Result<QString> get(const QString &key, const QString &fallback = QString()) const;
    Result<bool> set(const QString &key, const QString &value);
    Result<bool> remove(const QString &key);

private:
    Database &m_db;
};

} // namespace mini::storage
