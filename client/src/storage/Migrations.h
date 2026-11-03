#pragma once

#include "core/Result.h"

#include <QString>

namespace mini::storage {

class Database;

// Schema is a sequence of embedded SQL migrations tracked in schema_version.
// A copy of the database is written to `backupsDir` before upgrading an
// existing file, so a failed migration can be rolled back by hand.
class Migrations
{
public:
    Migrations() = delete;

    static constexpr int latestVersion = 7;

    [[nodiscard]] static Result<int> currentVersion(Database &db);
    static Result<bool> apply(Database &db, const QString &backupsDir = QString());
};

} // namespace mini::storage
