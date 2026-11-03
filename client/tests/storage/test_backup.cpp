#include "storage/Backup.h"
#include "storage/ChatsRepo.h"
#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace mini;
using namespace mini::storage;

namespace {

Chat makeChat(const QString &chatId, const QString &title)
{
    Chat chat;
    chat.chatId = chatId;
    chat.title = title;
    return chat;
}

void writeKeysFile(const QString &path)
{
    QFile keys(path);
    QVERIFY(keys.open(QIODevice::WriteOnly));
    keys.write(QByteArray("TEST-KEYS-MARKER-0123456789"));
    keys.close();
}

} // namespace

class TestBackup : public QObject
{
    Q_OBJECT

private slots:
    void createsCopyWithKeysAndKeepsThree();
    void dailyBackupRunsOncePerDayAndButtonCreatesCopy();
    void restoresTheBackedUpState();
};

void TestBackup::createsCopyWithKeysAndKeepsThree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString keysPath = dir.filePath(QStringLiteral("keys.bin"));
    const QString backups = dir.filePath(QStringLiteral("backups"));
    writeKeysFile(keysPath);

    StorageWorker worker;
    QVERIFY2(worker.open(path, test::dbKey(), backups).isOk(), "open failed");

    QVector<QString> names;
    for (int i = 0; i < 4; ++i) {
        auto created = worker.submit([&keysPath, &backups](Database &db) {
            auto backup = Backup::create(db, keysPath, backups, 3);
            return backup.isOk() ? backup.value().directory : QString();
        });
        created.waitForFinished();
        QVERIFY(!created.result().isEmpty());
        names.push_back(created.result());
    }

    auto listed = worker.submit([&backups](Database &) {
        return Backup::list(backups);
    });
    listed.waitForFinished();
    const QVector<BackupInfo> backupsNow = listed.result();
    QCOMPARE(backupsNow.size(), 3);
    const QString oldest = QDir(names.first()).absolutePath();
    const bool oldestWasPruned =
        std::none_of(backupsNow.cbegin(), backupsNow.cend(),
                     [&oldest](const BackupInfo &info) { return info.directory == oldest; });
    QVERIFY(oldestWasPruned);

    const BackupInfo newest = backupsNow.constLast();
    QVERIFY(QFile::exists(newest.databasePath));
    QVERIFY(QFile::exists(newest.keysPath));
    QFile keys(newest.keysPath);
    QVERIFY(keys.open(QIODevice::ReadOnly));
    QCOMPARE(keys.readAll(), QByteArray("TEST-KEYS-MARKER-0123456789"));
}

void TestBackup::dailyBackupRunsOncePerDayAndButtonCreatesCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString keysPath = dir.filePath(QStringLiteral("keys.bin"));
    const QString backups = dir.filePath(QStringLiteral("backups"));
    writeKeysFile(keysPath);

    StorageWorker worker;
    QVERIFY2(worker.open(path, test::dbKey(), backups, keysPath).isOk(), "open failed");

    // The daily backup runs during open() for this launch.
    auto afterOpen = worker.submit([&backups](Database &) { return Backup::list(backups); });
    afterOpen.waitForFinished();
    QCOMPARE(afterOpen.result().size(), 1);

    // The "Backup now" button path.
    auto button = worker.createBackup(keysPath, backups);
    button.waitForFinished();
    QVERIFY(!button.result().isEmpty());

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 tomorrow = QDateTime::currentDateTime().addDays(1).toMSecsSinceEpoch();

    auto sameDay = worker.submit([&keysPath, &backups, now](Database &db) {
        auto result = Backup::createDailyIfDue(db, keysPath, backups, 3, now);
        return result.isOk() && !result.value().has_value();
    });
    sameDay.waitForFinished();
    QVERIFY2(sameDay.result(), "a second backup was created on the same day");

    auto nextDay = worker.submit([&keysPath, &backups, tomorrow](Database &db) {
        auto result = Backup::createDailyIfDue(db, keysPath, backups, 3, tomorrow);
        return result.isOk() && result.value().has_value();
    });
    nextDay.waitForFinished();
    QVERIFY2(nextDay.result(), "the daily backup did not run on the next day");

    auto listed = worker.submit([&backups](Database &) { return Backup::list(backups); });
    listed.waitForFinished();
    QCOMPARE(listed.result().size(), 3);
}

void TestBackup::restoresTheBackedUpState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString keysPath = dir.filePath(QStringLiteral("keys.bin"));
    const QString backups = dir.filePath(QStringLiteral("backups"));
    writeKeysFile(keysPath);

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey(), backups).isOk(), "open failed");
        auto seeded = worker.submit([&keysPath, &backups](Database &db) {
            ChatsRepo(db).upsert(makeChat(QStringLiteral("a"), QStringLiteral("Alice")));
            return Backup::create(db, keysPath, backups, 3).isOk();
        });
        seeded.waitForFinished();
        QVERIFY(seeded.result());

        auto changed = worker.submit([](Database &db) {
            return ChatsRepo(db).upsert(makeChat(QStringLiteral("b"), QStringLiteral("Bob"))).isOk();
        });
        changed.waitForFinished();
        QVERIFY(changed.result());
        worker.close();
    }

    const auto backup = Backup::newest(backups);
    QVERIFY(backup.has_value());
    auto restored = Backup::restore(*backup, path, keysPath);
    QVERIFY2(restored.isOk(), qPrintable(restored.error()));

    StorageWorker reopened;
    QVERIFY2(reopened.open(path, test::dbKey(), backups).isOk(), "reopen failed");
    auto chats = reopened.submit([](Database &db) {
        auto listed = ChatsRepo(db).list();
        return listed.isOk() ? listed.value() : QVector<Chat>();
    });
    chats.waitForFinished();
    QCOMPARE(chats.result().size(), 1);
    QCOMPARE(chats.result().first().chatId, QStringLiteral("a"));
}

QTEST_GUILESS_MAIN(TestBackup)
#include "test_backup.moc"
