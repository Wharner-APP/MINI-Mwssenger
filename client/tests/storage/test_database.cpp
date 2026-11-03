#include "storage/Backup.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/Migrations.h"
#include "storage/Sql.h"
#include "storage/StorageWorker.h"

#include "StorageTestSupport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sqlite3.h>

using namespace mini;
using namespace mini::storage;

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void createsSchemaAndReopens();
    void survivesUncleanShutdownThroughWal();
    void corruptedFileRestoresFromBackup();
    void corruptedFileWithoutBackupFailsCleanly();
    void rejectsShortDbKey();
    void upgradeFromV1SealsPreviewsAndScrubsFreelist();
};

void TestDatabase::createsSchemaAndReopens()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));

    {
        StorageWorker worker;
        auto opened = worker.open(path, test::dbKey());
        QVERIFY2(opened.isOk(), qPrintable(opened.error()));

        auto version = worker.submit([](Database &db) {
            auto current = Migrations::currentVersion(db);
            return current.isOk() ? current.value() : -1;
        });
        version.waitForFinished();
        QCOMPARE(version.result(), Migrations::latestVersion);

        auto journal = worker.submit([](Database &db) {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(db.handle(), "PRAGMA journal_mode", -1, &statement, nullptr)
                != SQLITE_OK) {
                return QByteArray();
            }
            QByteArray mode;
            if (sqlite3_step(statement) == SQLITE_ROW) {
                mode = QByteArray(reinterpret_cast<const char *>(
                    sqlite3_column_text(statement, 0)));
            }
            sqlite3_finalize(statement);
            return mode;
        });
        journal.waitForFinished();
        QCOMPARE(journal.result().toLower(), QByteArray("wal"));

        auto inserted = worker.submit([](Database &db) {
            Chat chat;
            chat.chatId = QStringLiteral("chat-1");
            chat.title = QStringLiteral("Alice");
            chat.peerLogin = QStringLiteral("alice");
            return ChatsRepo(db).upsert(chat).isOk();
        });
        inserted.waitForFinished();
        QVERIFY(inserted.result());
    }

    QVERIFY(QFile::exists(path));

    StorageWorker reopened;
    auto opened = reopened.open(path, test::dbKey());
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));

    auto version = reopened.submit([](Database &db) { return db.schemaVersion(); });
    version.waitForFinished();
    QCOMPARE(version.result(), Migrations::latestVersion);

    auto chats = reopened.submit([](Database &db) {
        auto listed = ChatsRepo(db).list();
        return listed.isOk() ? listed.value() : QVector<Chat>();
    });
    chats.waitForFinished();
    QCOMPARE(chats.result().size(), 1);
    QCOMPARE(chats.result().first().title, QStringLiteral("Alice"));
}

void TestDatabase::survivesUncleanShutdownThroughWal()
{
    QTemporaryDir dir;
    QTemporaryDir copyDir;
    QVERIFY(dir.isValid());
    QVERIFY(copyDir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString copyPath = copyDir.filePath(QStringLiteral("messages.db"));

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
        auto written = worker.submit([](Database &db) {
            // Keep committed pages in the WAL instead of the main file.
            db.exec(QByteArrayLiteral("PRAGMA wal_autocheckpoint=0"));
            Chat chat;
            chat.chatId = QStringLiteral("chat-wal");
            chat.title = QStringLiteral("From WAL");
            return ChatsRepo(db).upsert(chat).isOk();
        });
        written.waitForFinished();
        QVERIFY(written.result());

        // Simulate an unclean shutdown: copy the file pair while the writer is
        // alive, then never close the original in an orderly way.
        QVERIFY(QFile::copy(path, copyPath));
        QVERIFY(QFile::exists(path + QStringLiteral("-wal")));
        QVERIFY(QFile::copy(path + QStringLiteral("-wal"),
                            copyPath + QStringLiteral("-wal")));
    }

    StorageWorker replayed;
    QVERIFY2(replayed.open(copyPath, test::dbKey()).isOk(), "replay failed");
    auto chats = replayed.submit([](Database &db) {
        auto listed = ChatsRepo(db).list();
        return listed.isOk() ? listed.value() : QVector<Chat>();
    });
    chats.waitForFinished();
    QCOMPARE(chats.result().size(), 1);
    QCOMPARE(chats.result().first().title, QStringLiteral("From WAL"));
}

void TestDatabase::corruptedFileRestoresFromBackup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QString backups = dir.filePath(QStringLiteral("backups"));

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey(), backups).isOk(), "open failed");
        auto seeded = worker.submit([&backups](Database &db) {
            Chat chat;
            chat.chatId = QStringLiteral("chat-1");
            chat.title = QStringLiteral("Survivor");
            const bool inserted = ChatsRepo(db).upsert(chat).isOk();
            const bool backedUp = Backup::create(db, QString(), backups, 3).isOk();
            return inserted && backedUp;
        });
        seeded.waitForFinished();
        QVERIFY(seeded.result());
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(0));
    QCOMPARE(file.write(QByteArray(120, '\xAB')), qint64(120));
    file.close();

    StorageWorker worker;
    auto opened = worker.open(path, test::dbKey(), backups);
    QVERIFY2(opened.isOk(), qPrintable(opened.error()));
    QVERIFY(worker.recoveredFromBackup());
    QVERIFY(!worker.recoveryMessage().isEmpty());

    auto chats = worker.submit([](Database &db) {
        auto listed = ChatsRepo(db).list();
        return listed.isOk() ? listed.value() : QVector<Chat>();
    });
    chats.waitForFinished();
    QCOMPARE(chats.result().size(), 1);
    QCOMPARE(chats.result().first().title, QStringLiteral("Survivor"));
}

void TestDatabase::corruptedFileWithoutBackupFailsCleanly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(0));
    file.write(QByteArray(120, '\xAB'));
    file.close();

    StorageWorker worker;
    auto opened = worker.open(path, test::dbKey());
    QVERIFY(opened.isError());
    QVERIFY(!worker.isOpen());
}

void TestDatabase::rejectsShortDbKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StorageWorker worker;
    auto opened = worker.open(dir.filePath(QStringLiteral("messages.db")),
                              QByteArray(16, 'x'));
    QVERIFY(opened.isError());
    QVERIFY(opened.error().contains(QStringLiteral("DB_KEY")));
}

void TestDatabase::upgradeFromV1SealsPreviewsAndScrubsFreelist()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("messages.db"));
    const QByteArray marker = QByteArrayLiteral("PLAINTEXT-PREVIEW-MARKER");

    {
        StorageWorker worker;
        QVERIFY2(worker.open(path, test::dbKey()).isOk(), "open failed");
        auto seeded = worker.submit([marker](Database &db) {
            // Simulate a v1 database: plaintext preview and schema_version 1.
            const QByteArray sql =
                QByteArrayLiteral("INSERT INTO chats(chat_id, last_message_preview) VALUES("
                                  "'legacy', '")
                + marker + QByteArrayLiteral("');"
                  "UPDATE schema_version SET version = 1;");
            return db.exec(sql).isOk();
        });
        seeded.waitForFinished();
        QVERIFY(seeded.result());
    }

    StorageWorker upgraded;
    QVERIFY2(upgraded.open(path, test::dbKey()).isOk(), "upgrade failed");
    auto version = upgraded.submit([](Database &db) { return db.schemaVersion(); });
    version.waitForFinished();
    QCOMPARE(version.result(), Migrations::latestVersion);

    auto preview = upgraded.submit([](Database &db) {
        auto listed = ChatsRepo(db).list();
        return listed.isOk() && !listed.value().isEmpty()
            ? listed.value().first().lastMessagePreview
            : QString();
    });
    preview.waitForFinished();
    QCOMPARE(preview.result(), QString::fromLatin1(marker));

    auto rawPreview = upgraded.submit([marker](Database &db) {
        detail::Statement select(
            db.handle(), "SELECT last_message_preview FROM chats WHERE chat_id = 'legacy'");
        return select.isOk() && select.step() == SQLITE_ROW ? select.blob(0) : QByteArray();
    });
    rawPreview.waitForFinished();
    QVERIFY(!rawPreview.result().contains(marker));

    auto secure = upgraded.submit([](Database &db) {
        detail::Statement pragma(db.handle(), "PRAGMA secure_delete");
        return pragma.isOk() && pragma.step() == SQLITE_ROW ? pragma.integer(0) : 0;
    });
    secure.waitForFinished();
    QCOMPARE(secure.result(), 1);
}

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
