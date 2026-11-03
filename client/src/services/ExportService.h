#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

// §49.1 options. `chatIds` empty means every chat. Secret chats do not exist
// in this build; the flag is kept so the manifest matches the specification.
struct ExportOptions
{
    QStringList chatIds;
    bool includeMedia = false;
    bool includeSecretChats = false;
    QString outputPath;
};

struct ExportSummary
{
    QString path;
    int chatCount = 0;
    int messageCount = 0;
    int mediaCount = 0;
    qint64 bytes = 0;
    bool cancelled = false;
};

// §49.1: writes a single encrypted .minibackup file. Database work runs on the
// StorageWorker thread; the completion callback is delivered on the thread
// that owns the service (QFutureWatcher), so the UI never blocks.
class ExportService : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(Result<ExportSummary>)>;

    explicit ExportService(storage::StorageWorker *storage, QObject *parent = nullptr);

    // K_mac from the sign.in key branch (AuthService::fileKeys().kmac).
    void setKMac(const QByteArray &kMac);
    void setUserId(const QString &userId);
    // Root of the MediaCache (data/media); media originals are read from
    // <root>/cache.
    void setMediaRoot(const QString &root);

    void exportBackup(const ExportOptions &options, Done done);

    // Thread-safe. The archive stops between messages and leaves a partial
    // file uncommitted; the callback reports cancelled=true.
    void cancel();

signals:
    void progressChanged(int percent, const QString &stage);

private:
    storage::StorageWorker *m_storage = nullptr;
    QByteArray m_kMac;
    QString m_userId;
    QString m_mediaRoot;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace mini::services
