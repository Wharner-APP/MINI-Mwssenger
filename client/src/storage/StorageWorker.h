#pragma once

#include "core/Result.h"
#include "storage/Database.h"

#include <QByteArray>
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <QSemaphore>
#include <QString>
#include <QThread>

#include <atomic>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mini::storage {

// The only way to reach SQLite. Owns one database connection and one worker
// thread; submit() queues a callable that receives Database& and returns a
// QFuture for its result. Tasks run strictly in submission order.
class StorageWorker : public QObject
{
    Q_OBJECT

public:
    explicit StorageWorker(QObject *parent = nullptr);
    ~StorageWorker() override;

    StorageWorker(const StorageWorker &) = delete;
    StorageWorker &operator=(const StorageWorker &) = delete;

    // When backupsDir and keysPath are both set, open() also runs the
    // once-a-day backup (R392) for this launch.
    Result<bool> open(const QString &dbPath, const QByteArray &dbKey,
                      const QString &backupsDir = QString(),
                      const QString &keysPath = QString());
    void close();

    // R392: "Backup now" and the once-a-day job both run on this thread.
    // The returned string is the backup folder, empty when nothing was made.
    QFuture<QString> createBackup(const QString &keysPath, const QString &backupsDir,
                                  int keep = 3);
    QFuture<QString> createDailyBackupIfDue(const QString &keysPath,
                                            const QString &backupsDir, int keep = 3);

    [[nodiscard]] bool isOpen() const { return m_open.load(); }
    [[nodiscard]] bool recoveredFromBackup() const { return m_recovered.load(); }
    [[nodiscard]] QString recoveryMessage() const { return m_recoveryMessage; }

    template <typename Fn>
    auto submit(Fn &&fn) -> QFuture<std::invoke_result_t<std::decay_t<Fn>, Database &>>
    {
        using T = std::invoke_result_t<std::decay_t<Fn>, Database &>;
        auto promise = std::make_shared<QPromise<T>>();
        promise->start();
        QFuture<T> future = promise->future();

        auto callable = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(fn));
        auto task = std::make_shared<Task>([this, promise, callable]() mutable {
            try {
                if (!m_db) {
                    throw std::runtime_error("storage worker is not open");
                }
                if constexpr (std::is_void_v<T>) {
                    (*callable)(*m_db);
                } else {
                    promise->addResult((*callable)(*m_db));
                }
            } catch (const std::exception &error) {
                emit taskFailed(QString::fromUtf8(error.what()));
                if constexpr (!std::is_void_v<T> && std::is_default_constructible_v<T>) {
                    promise->addResult(T{});
                }
            } catch (...) {
                emit taskFailed(QStringLiteral("storage task failed"));
                if constexpr (!std::is_void_v<T> && std::is_default_constructible_v<T>) {
                    promise->addResult(T{});
                }
            }
            promise->finish();
        });
        enqueue(std::move(task));
        return future;
    }

signals:
    void databaseOpened(int schemaVersion);
    void databaseFailed(const QString &message);
    // A submitted callable threw; its future carries a default value.
    void taskFailed(const QString &message);

private:
    using Task = std::function<void()>;

    void enqueue(std::shared_ptr<Task> task);
    void runLoop();
    void stop();

    std::unique_ptr<QThread> m_thread;
    std::unique_ptr<Database> m_db;
    QMutex m_taskMutex;
    QSemaphore m_wake;
    std::deque<std::shared_ptr<Task>> m_tasks;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_open{false};
    std::atomic<bool> m_recovered{false};
    QString m_recoveryMessage;
};

} // namespace mini::storage
