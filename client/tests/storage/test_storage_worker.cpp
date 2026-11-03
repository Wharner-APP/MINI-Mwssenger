#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::storage;

class TestStorageWorker : public QObject
{
    Q_OBJECT

private slots:
    void runsTasksOnSingleWorkerThread();
    void preservesSubmissionOrder();
    void reportsClosedWorkerThroughSignal();
};

void TestStorageWorker::runsTasksOnSingleWorkerThread()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    auto opened = worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey());
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));

    QMutex mutex;
    QVector<Qt::HANDLE> threads;
    for (int i = 0; i < 10; ++i) {
        auto future = worker.submit([&mutex, &threads](Database &) {
            QMutexLocker locker(&mutex);
            threads.push_back(QThread::currentThreadId());
        });
        future.waitForFinished();
    }

    QCOMPARE(threads.size(), 10);
    QVERIFY(threads.first() != QThread::currentThreadId());
    for (const Qt::HANDLE handle : threads) {
        QVERIFY(handle == threads.first());
    }
}

void TestStorageWorker::preservesSubmissionOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    QMutex mutex;
    QVector<int> order;
    auto first = worker.submit([&mutex, &order](Database &) {
        QMutexLocker locker(&mutex);
        order.push_back(1);
    });
    auto second = worker.submit([&mutex, &order](Database &) {
        QMutexLocker locker(&mutex);
        order.push_back(2);
        return 2;
    });

    first.waitForFinished();
    second.waitForFinished();

    QCOMPARE(second.result(), 2);
    QCOMPARE(order.size(), 2);
    QCOMPARE(order.at(0), 1);
    QCOMPARE(order.at(1), 2);
}

void TestStorageWorker::reportsClosedWorkerThroughSignal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    QVERIFY2(worker.open(dir.filePath(QStringLiteral("messages.db")), test::dbKey()).isOk(),
             "open failed");

    QSignalSpy failures(&worker, &StorageWorker::taskFailed);
    worker.close();

    auto future = worker.submit([](Database &) { return 7; });
    future.waitForFinished();

    QCOMPARE(future.result(), 0);
    QCOMPARE(failures.count(), 1);
}

QTEST_GUILESS_MAIN(TestStorageWorker)
#include "test_storage_worker.moc"
