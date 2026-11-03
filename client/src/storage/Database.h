#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

struct sqlite3;

namespace mini::storage {

// One SQLite connection with WAL enabled. The object is owned by StorageWorker
// and must only be touched from the worker thread.
//
// DB_KEY is not an SQLCipher key: SQLite stays a plain file and only message
// bodies are sealed with XChaCha20-Poly1305 (spec §4.5). DB_KEY (32 bytes) is
// the AEAD key for those bodies.
class Database
{
public:
    // Opens (creating when missing) the file, runs integrity checks, restores
    // the newest backup if the file is corrupted, then applies migrations.
    // `backupsDir` may be empty: corruption is then reported without recovery.
    static Result<std::unique_ptr<Database>> open(const QString &path,
                                                  const QByteArray &dbKey,
                                                  const QString &backupsDir = QString());

    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    [[nodiscard]] sqlite3 *handle() const { return m_db; }
    [[nodiscard]] const QByteArray &dbKey() const { return m_dbKey; }
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] bool isOpen() const { return m_db != nullptr; }
    [[nodiscard]] int schemaVersion() const;

    // True when open() recovered a corrupted file from backupsDir.
    [[nodiscard]] bool recoveredFromBackup() const { return m_recovered; }
    [[nodiscard]] QString recoveryMessage() const { return m_recoveryMessage; }

    // Executes raw SQL (used by migrations). Multi-statement input is allowed.
    Result<bool> exec(const QByteArray &sql);

    // BEGIN IMMEDIATE / COMMIT around fn; ROLLBACK on either error.
    Result<bool> transaction(const std::function<Result<bool>(Database &)> &fn);

    void close();

private:
    Database(QString path, QByteArray dbKey);

    bool openConnection(QString *error);
    bool checkIntegrity(QString *error);
    bool recoverFromBackup(const QString &backupsDir, QString *recoveryMessage, QString *error);

    sqlite3 *m_db = nullptr;
    QString m_path;
    QByteArray m_dbKey;
    bool m_recovered = false;
    QString m_recoveryMessage;
};

} // namespace mini::storage
