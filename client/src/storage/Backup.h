#pragma once

#include "core/Result.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

struct BackupInfo
{
    QString directory;
    QString databasePath;
    QString keysPath;
    qint64 sizeBytes = 0;
    qint64 createdAt = 0;
};

// Copies the live database through the SQLite online-backup API. The caller
// must run it on the thread that owns the connection.
Result<bool> copyDatabaseFile(Database &db, const QString &targetPath);

// data/backups/<backup-yyyyMMdd-HHmmss>/{messages.db, keys.bin}
// create() keeps only the newest `keep` backups.
class Backup
{
public:
    Backup() = delete;

    static Result<BackupInfo> create(Database &db, const QString &keysPath,
                                     const QString &backupsDir, int keep = 3);
    // R392: one backup per calendar day. Returns nullopt when today's copy
    // already exists. `nowMs` is injectable for tests (0 = wall clock).
    static Result<std::optional<BackupInfo>> createDailyIfDue(Database &db,
                                                              const QString &keysPath,
                                                              const QString &backupsDir,
                                                              int keep = 3,
                                                              qint64 nowMs = 0);
    [[nodiscard]] static bool isDailyBackupDue(const QString &backupsDir, qint64 nowMs = 0);
    static Result<bool> restore(const BackupInfo &backup, const QString &databasePath,
                                const QString &keysPath);
    [[nodiscard]] static QVector<BackupInfo> list(const QString &backupsDir);
    [[nodiscard]] static std::optional<BackupInfo> newest(const QString &backupsDir);
};

} // namespace mini::storage
