#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

struct ImportOptions
{
    QString inputPath;
    // §49.2: the live database is copied here before the first write. An
    // empty value falls back to setBackupLocation().
    QString backupDir;
    bool restoreMedia = true;
};

struct ImportSummary
{
    int chatsAdded = 0;
    int messagesAdded = 0;
    int messagesSkipped = 0;
    int mediaImported = 0;
    bool cancelled = false;
};

// §49.2: verifies magic/version, decrypts with the sign.in key, merges by
// (chat_id, client_msg_id), copies media into the cache and backs up the
// database before touching it. Cancellation is checked between messages.
class ImportService : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(Result<ImportSummary>)>;

    explicit ImportService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setKMac(const QByteArray &kMac);
    void setMediaRoot(const QString &root);
    // backupsDir receives a storage::Backup folder; keysPath may be empty in
    // tests and portable setups without keys.bin.
    void setBackupLocation(const QString &backupsDir, const QString &keysPath);

    void importBackup(const ImportOptions &options, Done done);

    // Thread-safe. An import cancelled before it starts returns
    // cancelled=true without creating a backup or writing rows.
    void cancel();

signals:
    void progressChanged(int percent, const QString &stage);

private:
    storage::StorageWorker *m_storage = nullptr;
    QByteArray m_kMac;
    QString m_mediaRoot;
    QString m_backupDir;
    QString m_keysPath;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace mini::services
