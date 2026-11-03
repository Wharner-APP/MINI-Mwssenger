#include "storage/StorageWorker.h"

#include "storage/Backup.h"
#include "storage/MessagesRepo.h"

namespace mini::storage {

StorageWorker::StorageWorker(QObject *parent) : QObject(parent)
{
    m_thread.reset(QThread::create([this]() { runLoop(); }));
    m_thread->start();
}

StorageWorker::~StorageWorker()
{
    close();
    stop();
}

Result<bool> StorageWorker::open(const QString &dbPath, const QByteArray &dbKey,
                                 const QString &backupsDir, const QString &keysPath)
{
    if (m_open.load()) {
        return Result<bool>::error(QStringLiteral("storage worker is already open"));
    }
    if (QThread::currentThread() == m_thread.get()) {
        return Result<bool>::error(QStringLiteral("open() cannot run on the storage thread"));
    }

    auto outcome = std::make_shared<Result<bool>>(
        Result<bool>::error(QStringLiteral("storage worker stopped before opening")));
    auto done = std::make_shared<QSemaphore>();

    enqueue(std::make_shared<Task>([this, dbPath, dbKey, backupsDir, keysPath, outcome, done]() {
        auto opened = Database::open(dbPath, dbKey, backupsDir);
        if (opened.isOk()) {
            m_db = std::move(opened.value());
            m_open.store(true);
            m_recovered.store(m_db->recoveredFromBackup());
            m_recoveryMessage = m_db->recoveryMessage();

            // §4.8: the search index is in-memory and rebuilt on every start.
            if (auto reindexed = MessagesRepo(*m_db).rebuildSearchIndex();
                reindexed.isError()) {
                emit taskFailed(reindexed.error());
            }

            // R392: at most one automatic backup per calendar day, made at launch.
            if (!backupsDir.isEmpty() && !keysPath.isEmpty()) {
                auto daily = Backup::createDailyIfDue(*m_db, keysPath, backupsDir, 3);
                if (daily.isError()) {
                    emit taskFailed(daily.error());
                }
            }
            *outcome = Result<bool>::ok(true);
            emit databaseOpened(m_db->schemaVersion());
        } else {
            *outcome = Result<bool>::error(opened.error());
            emit databaseFailed(opened.error());
        }
        done->release();
    }));
    done->acquire();
    return *outcome;
}

void StorageWorker::close()
{    if (!m_open.exchange(false)) {
        return;
    }
    if (QThread::currentThread() == m_thread.get()) {
        m_db.reset();
        return;
    }

    auto done = std::make_shared<QSemaphore>();
    enqueue(std::make_shared<Task>([this, done]() {
        m_db.reset();
        done->release();
    }));
    done->acquire();
}

QFuture<QString> StorageWorker::createBackup(const QString &keysPath,
                                            const QString &backupsDir, int keep)
{
    return submit([this, keysPath, backupsDir, keep](Database &db) -> QString {
        auto created = Backup::create(db, keysPath, backupsDir, keep);
        if (created.isError()) {
            emit taskFailed(created.error());
            return QString();
        }
        return created.value().directory;
    });
}

QFuture<QString> StorageWorker::createDailyBackupIfDue(const QString &keysPath,
                                                       const QString &backupsDir, int keep)
{
    return submit([this, keysPath, backupsDir, keep](Database &db) -> QString {
        auto created = Backup::createDailyIfDue(db, keysPath, backupsDir, keep);
        if (created.isError()) {
            emit taskFailed(created.error());
            return QString();
        }
        if (!created.value().has_value()) {
            return QString(); // today's copy is already on disk
        }
        return created.value()->directory;
    });
}

void StorageWorker::enqueue(std::shared_ptr<Task> task)
{
    {
        QMutexLocker locker(&m_taskMutex);
        m_tasks.push_back(std::move(task));
    }
    m_wake.release();
}

void StorageWorker::runLoop()
{
    for (;;) {
        m_wake.acquire();

        std::shared_ptr<Task> task;
        {
            QMutexLocker locker(&m_taskMutex);
            if (!m_tasks.empty()) {
                task = m_tasks.front();
                m_tasks.pop_front();
            }
        }
        if (task) {
            (*task)();
            continue;
        }
        if (m_stopping.load()) {
            break;
        }
    }
}

void StorageWorker::stop()
{
    if (!m_thread) {
        return;
    }
    m_stopping.store(true);
    m_wake.release();
    m_thread->wait();
    m_thread.reset();

    // Tasks queued after the sentinel would otherwise keep futures alive forever.
    QMutexLocker locker(&m_taskMutex);
    m_tasks.clear();
}

} // namespace mini::storage
