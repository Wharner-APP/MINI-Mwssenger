#include "storage/Backup.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sqlite3.h>

namespace mini::storage {
namespace {

constexpr auto kBackupPrefix = "backup-";
constexpr auto kDatabaseFileName = "messages.db";
constexpr auto kKeysFileName = "keys.bin";

QString uniqueBackupName(const QString &backupsDir)
{
    const QString base =
        QString::fromLatin1(kBackupPrefix)
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name = base;
    int suffix = 1;
    while (QFileInfo::exists(QDir(backupsDir).filePath(name))) {
        name = base + QLatin1Char('-') + QString::number(suffix++);
    }
    return name;
}

void removeSidecars(const QString &databasePath)
{
    QFile::remove(databasePath + QStringLiteral("-wal"));
    QFile::remove(databasePath + QStringLiteral("-shm"));
}

} // namespace

Result<bool> copyDatabaseFile(Database &db, const QString &targetPath)
{
    if (!db.handle()) {
        return Result<bool>::error(QStringLiteral("database is not open"));
    }
    if (QFileInfo::exists(targetPath)) {
        QFile::remove(targetPath);
    }

    sqlite3 *destination = nullptr;
    const QByteArray targetUtf8 = targetPath.toUtf8();
    if (sqlite3_open_v2(targetUtf8.constData(), &destination,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
        != SQLITE_OK) {
        const QString error = destination
                                  ? QString::fromUtf8(sqlite3_errmsg(destination))
                                  : QStringLiteral("cannot create the copy");
        if (destination) {
            sqlite3_close_v2(destination);
        }
        return Result<bool>::error(error);
    }

    // Fold the WAL into the main file so the copy does not depend on sidecars.
    sqlite3_exec(db.handle(), "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr);

    sqlite3_backup *backup = sqlite3_backup_init(destination, "main", db.handle(), "main");
    if (!backup) {
        const QString error = QString::fromUtf8(sqlite3_errmsg(destination));
        sqlite3_close_v2(destination);
        return Result<bool>::error(error);
    }
    const int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);

    const QString error = rc == SQLITE_DONE ? QString()
                                             : QString::fromUtf8(sqlite3_errmsg(destination));
    sqlite3_close_v2(destination);
    if (rc != SQLITE_DONE) {
        QFile::remove(targetPath);
        return Result<bool>::error(QStringLiteral("database backup failed: %1").arg(error));
    }
    return Result<bool>::ok(true);
}

Result<BackupInfo> Backup::create(Database &db, const QString &keysPath,
                                  const QString &backupsDir, int keep)
{
    if (backupsDir.isEmpty()) {
        return Result<BackupInfo>::error(QStringLiteral("no backup directory configured"));
    }
    QDir root(backupsDir);
    if (!root.mkpath(QStringLiteral("."))) {
        return Result<BackupInfo>::error(
            QStringLiteral("cannot create the backup directory: %1").arg(backupsDir));
    }

    const QString name = uniqueBackupName(backupsDir);
    const QString directory = root.filePath(name);
    if (!root.mkpath(name)) {
        return Result<BackupInfo>::error(QStringLiteral("cannot create the backup folder"));
    }

    BackupInfo info;
    info.directory = directory;
    info.databasePath = QDir(directory).filePath(QString::fromLatin1(kDatabaseFileName));
    info.keysPath = QDir(directory).filePath(QString::fromLatin1(kKeysFileName));

    if (auto copied = copyDatabaseFile(db, info.databasePath); copied.isError()) {
        QDir(directory).removeRecursively();
        return Result<BackupInfo>::error(copied.error());
    }

    if (!keysPath.isEmpty() && QFileInfo::exists(keysPath)) {
        if (!QFile::copy(keysPath, info.keysPath)) {
            QDir(directory).removeRecursively();
            return Result<BackupInfo>::error(QStringLiteral("cannot copy keys.bin"));
        }
    }

    info.sizeBytes = QFileInfo(info.databasePath).size();
    info.createdAt = QFileInfo(info.databasePath).lastModified().toSecsSinceEpoch();

    const QVector<BackupInfo> all = list(backupsDir);
    for (int i = 0; i < all.size() - qMax(1, keep); ++i) {
        QDir(all.at(i).directory).removeRecursively();
    }

    return Result<BackupInfo>::ok(info);
}

bool Backup::isDailyBackupDue(const QString &backupsDir, qint64 nowMs)
{
    const auto newestBackup = newest(backupsDir);
    if (!newestBackup.has_value()) {
        return true;
    }
    const qint64 now = nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
    const QDate today = QDateTime::fromMSecsSinceEpoch(now).date();
    const QDate lastBackup =
        QDateTime::fromSecsSinceEpoch(newestBackup->createdAt).date();
    return lastBackup < today;
}

Result<std::optional<BackupInfo>> Backup::createDailyIfDue(Database &db,
                                                           const QString &keysPath,
                                                           const QString &backupsDir,
                                                           int keep, qint64 nowMs)
{
    if (!isDailyBackupDue(backupsDir, nowMs)) {
        return Result<std::optional<BackupInfo>>::ok(std::nullopt);
    }
    auto created = create(db, keysPath, backupsDir, keep);
    if (created.isError()) {
        return Result<std::optional<BackupInfo>>::error(created.error());
    }
    return Result<std::optional<BackupInfo>>::ok(created.value());
}

Result<bool> Backup::restore(const BackupInfo &backup, const QString &databasePath,
                             const QString &keysPath)
{
    if (!QFileInfo::exists(backup.databasePath)) {
        return Result<bool>::error(QStringLiteral("backup file is missing"));
    }
    if (QFileInfo::exists(databasePath) && !QFile::remove(databasePath)) {
        return Result<bool>::error(QStringLiteral("cannot replace the current database"));
    }
    removeSidecars(databasePath);
    if (!QFile::copy(backup.databasePath, databasePath)) {
        return Result<bool>::error(QStringLiteral("cannot copy the database from the backup"));
    }
    if (!keysPath.isEmpty() && QFileInfo::exists(backup.keysPath)) {
        if (QFileInfo::exists(keysPath) && !QFile::remove(keysPath)) {
            return Result<bool>::error(QStringLiteral("cannot replace keys.bin"));
        }
        if (!QFile::copy(backup.keysPath, keysPath)) {
            return Result<bool>::error(QStringLiteral("cannot copy keys.bin from the backup"));
        }
    }
    return Result<bool>::ok(true);
}

QVector<BackupInfo> Backup::list(const QString &backupsDir)
{
    QVector<BackupInfo> backups;
    const QDir root(backupsDir);
    if (!root.exists()) {
        return backups;
    }
    const QStringList entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        if (!entry.startsWith(QString::fromLatin1(kBackupPrefix))) {
            continue;
        }
        const QDir directory(root.filePath(entry));
        const QString databasePath = directory.filePath(QString::fromLatin1(kDatabaseFileName));
        const QFileInfo databaseInfo(databasePath);
        if (!databaseInfo.exists()) {
            continue;
        }
        BackupInfo info;
        info.directory = directory.absolutePath();
        info.databasePath = databasePath;
        info.keysPath = directory.filePath(QString::fromLatin1(kKeysFileName));
        info.sizeBytes = databaseInfo.size();
        info.createdAt = databaseInfo.lastModified().toSecsSinceEpoch();
        backups.push_back(info);
    }
    return backups;
}

std::optional<BackupInfo> Backup::newest(const QString &backupsDir)
{
    const QVector<BackupInfo> all = list(backupsDir);
    if (all.isEmpty()) {
        return std::nullopt;
    }
    return all.constLast();
}

} // namespace mini::storage
