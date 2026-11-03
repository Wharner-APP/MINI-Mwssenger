#include "services/ExportService.h"
#include "services/ImportService.h"
#include "services/MediaCache.h"
#include "storage/Backup.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

const QByteArray kMac(32, '\x42');

template <typename T>
std::optional<Result<T>>
waitFor(const std::function<void(std::function<void(Result<T>)>)> &start)
{
    std::optional<Result<T>> value;
    start([&value](Result<T> result) { value = std::move(result); });
    for (int i = 0; i < 600 && !value.has_value(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return value;
}

bool seedMediaMessage(storage::StorageWorker &worker, const QString &chatId,
                      const QString &clientMsgId, const QString &mediaId)
{
    auto future = worker.submit([=](storage::Database &db) {
        Message message;
        message.clientMsgId = clientMsgId;
        message.chatId = chatId;
        message.body = QStringLiteral("caption");
        message.kind = MediaKind::Photo;
        message.mediaId = mediaId;
        message.mediaName = QStringLiteral("pic.jpg");
        message.mediaSize = 8;
        message.localTs = 300;
        message.outgoing = true;
        return storage::MessagesRepo(db).insertQueued(message).value();
    });
    future.waitForFinished();
    return future.result();
}

struct Exported
{
    QString path;
    ExportSummary summary;
};

std::optional<Exported> makeExport(storage::StorageWorker &worker, const QString &dir,
                                   const QString &fileName, bool media,
                                   const QString &mediaRoot = QString())
{
    ExportService service(&worker);
    service.setKMac(kMac);
    service.setUserId(QStringLiteral("user_1"));
    service.setMediaRoot(mediaRoot);

    ExportOptions options;
    options.outputPath = QDir(dir).filePath(fileName);
    options.includeMedia = media;
    auto result = waitFor<ExportSummary>([&service, options](auto done) {
        service.exportBackup(options, done);
    });
    if (!result.has_value() || result->isError()) {
        return std::nullopt;
    }
    return Exported{options.outputPath, result->value()};
}

} // namespace

class TestExportPhase4 : public QObject
{
    Q_OBJECT

private slots:
    void roundTripRestoresChatsMessagesAndMedia();
    void duplicateImportIsSkipped();
    void wrongKeyAndBadMagicAreRejected();
    void cancelStopsExportAndImport();
};

void TestExportPhase4::roundTripRestoresChatsMessagesAndMedia()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    storage::StorageWorker source;
    QVERIFY(source.open(dir.filePath(QStringLiteral("source.db")), dbKey()).isOk());
    QVERIFY(seedChat(source, QStringLiteral("chat_1"), QStringLiteral("alice")));
    QVERIFY(seedMessage(source, QStringLiteral("chat_1"), QStringLiteral("m1"),
                        QStringLiteral("hello"), 100));
    QVERIFY(seedMessage(source, QStringLiteral("chat_1"), QStringLiteral("m2"),
                        QStringLiteral("world"), 200));
    QVERIFY(seedMediaMessage(source, QStringLiteral("chat_1"), QStringLiteral("m3"),
                             QStringLiteral("up_1")));

    const QString sourceMedia = dir.filePath(QStringLiteral("media-src"));
    MediaCache sourceCache(sourceMedia);
    QVERIFY(sourceCache.ensureDirs());
    QVERIFY(!sourceCache.store(QStringLiteral("up_1"), QByteArrayLiteral("JPEGDATA"),
                               QStringLiteral("jpg"), false)
                 .isEmpty());

    ExportService exporter(&source);
    exporter.setKMac(kMac);
    exporter.setUserId(QStringLiteral("user_1"));
    exporter.setMediaRoot(sourceMedia);
    QSignalSpy progress(&exporter, &ExportService::progressChanged);

    ExportOptions options;
    options.outputPath = dir.filePath(QStringLiteral("archive.minibackup"));
    options.includeMedia = true;
    auto exported = waitFor<ExportSummary>([&](auto done) {
        exporter.exportBackup(options, done);
    });
    QVERIFY(exported.has_value());
    QVERIFY2(exported->isOk(), qPrintable(exported->error()));
    QCOMPARE(exported->value().chatCount, 1);
    QCOMPARE(exported->value().messageCount, 3);
    QCOMPARE(exported->value().mediaCount, 1);
    QVERIFY(exported->value().bytes > 0);
    QVERIFY(QFileInfo::exists(options.outputPath));
    QVERIFY(!progress.isEmpty());

    storage::StorageWorker target;
    QVERIFY(target.open(dir.filePath(QStringLiteral("target.db")), dbKey()).isOk());

    const QString targetMedia = dir.filePath(QStringLiteral("media-dst"));
    const QString backups = dir.filePath(QStringLiteral("backups"));
    ImportService importer(&target);
    importer.setKMac(kMac);
    importer.setMediaRoot(targetMedia);
    importer.setBackupLocation(backups, QString());
    QSignalSpy importProgress(&importer, &ImportService::progressChanged);

    ImportOptions importOptions;
    importOptions.inputPath = options.outputPath;
    importOptions.backupDir = backups;
    auto imported = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(importOptions, done);
    });
    QVERIFY(imported.has_value());
    QVERIFY2(imported->isOk(), qPrintable(imported->error()));
    QCOMPARE(imported->value().chatsAdded, 1);
    QCOMPARE(imported->value().messagesAdded, 3);
    QCOMPARE(imported->value().messagesSkipped, 0);
    QCOMPARE(imported->value().mediaImported, 1);
    QVERIFY(!importProgress.isEmpty());
    QVERIFY(!storage::Backup::list(backups).isEmpty());

    const auto restored = fetchMessage(target, QStringLiteral("chat_1"),
                                       QStringLiteral("m1"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->body, QStringLiteral("hello"));
    QCOMPARE(restored->outgoing, true);
    QCOMPARE(messageCount(target, QStringLiteral("chat_1")), 3);

    MediaCache targetCache(targetMedia);
    QVERIFY(!targetCache.existingPath(QStringLiteral("up_1"), false).isEmpty());
}

void TestExportPhase4::duplicateImportIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    storage::StorageWorker source;
    QVERIFY(source.open(dir.filePath(QStringLiteral("source.db")), dbKey()).isOk());
    QVERIFY(seedChat(source, QStringLiteral("chat_1"), QStringLiteral("alice")));
    QVERIFY(seedMessage(source, QStringLiteral("chat_1"), QStringLiteral("m1"),
                        QStringLiteral("hello"), 100));
    auto exported = makeExport(source, dir.path(), QStringLiteral("archive.minibackup"),
                               false);
    QVERIFY(exported.has_value());

    storage::StorageWorker target;
    QVERIFY(target.open(dir.filePath(QStringLiteral("target.db")), dbKey()).isOk());
    ImportService importer(&target);
    importer.setKMac(kMac);
    importer.setBackupLocation(dir.filePath(QStringLiteral("backups")), QString());

    ImportOptions options;
    options.inputPath = exported->path;
    auto first = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(options, done);
    });
    QVERIFY(first.has_value());
    QVERIFY(first->isOk());
    QCOMPARE(first->value().messagesAdded, 1);

    auto second = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(options, done);
    });
    QVERIFY(second.has_value());
    QVERIFY(second->isOk());
    QCOMPARE(second->value().chatsAdded, 0);
    QCOMPARE(second->value().messagesAdded, 0);
    QCOMPARE(second->value().messagesSkipped, 1);
    QCOMPARE(messageCount(target, QStringLiteral("chat_1")), 1);
}

void TestExportPhase4::wrongKeyAndBadMagicAreRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    storage::StorageWorker source;
    QVERIFY(source.open(dir.filePath(QStringLiteral("source.db")), dbKey()).isOk());
    QVERIFY(seedChat(source, QStringLiteral("chat_1"), QStringLiteral("alice")));
    QVERIFY(seedMessage(source, QStringLiteral("chat_1"), QStringLiteral("m1"),
                        QStringLiteral("secret"), 100));
    auto exported = makeExport(source, dir.path(), QStringLiteral("archive.minibackup"),
                               false);
    QVERIFY(exported.has_value());

    storage::StorageWorker target;
    QVERIFY(target.open(dir.filePath(QStringLiteral("target.db")), dbKey()).isOk());
    ImportService importer(&target);
    importer.setBackupLocation(dir.filePath(QStringLiteral("backups")), QString());

    ImportOptions options;
    options.inputPath = exported->path;

    importer.setKMac(QByteArray(32, '\x11'));
    auto wrongKey = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(options, done);
    });
    QVERIFY(wrongKey.has_value());
    QVERIFY(wrongKey->isError());
    QCOMPARE(wrongKey->error(), QStringLiteral("backup.decrypt_failed"));

    QFile original(exported->path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QByteArray bytes = original.readAll();
    original.close();

    QByteArray badMagic = bytes;
    badMagic[0] = 'X';
    const QString badMagicPath = dir.filePath(QStringLiteral("bad-magic.minibackup"));
    QFile magicFile(badMagicPath);
    QVERIFY(magicFile.open(QIODevice::WriteOnly));
    magicFile.write(badMagic);
    magicFile.close();

    importer.setKMac(kMac);
    options.inputPath = badMagicPath;
    auto magicResult = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(options, done);
    });
    QVERIFY(magicResult.has_value());
    QVERIFY(magicResult->isError());
    QCOMPARE(magicResult->error(), QStringLiteral("backup.bad_magic"));

    QByteArray badVersion = bytes;
    badVersion[8] = '\x02';
    const QString badVersionPath = dir.filePath(QStringLiteral("bad-version.minibackup"));
    QFile versionFile(badVersionPath);
    QVERIFY(versionFile.open(QIODevice::WriteOnly));
    versionFile.write(badVersion);
    versionFile.close();

    options.inputPath = badVersionPath;
    auto versionResult = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(options, done);
    });
    QVERIFY(versionResult.has_value());
    QVERIFY(versionResult->isError());
    QCOMPARE(versionResult->error(), QStringLiteral("backup.unsupported_version"));
}

void TestExportPhase4::cancelStopsExportAndImport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    storage::StorageWorker source;
    QVERIFY(source.open(dir.filePath(QStringLiteral("source.db")), dbKey()).isOk());
    QVERIFY(seedChat(source, QStringLiteral("chat_1"), QStringLiteral("alice")));
    for (int i = 0; i < 40; ++i) {
        QVERIFY(seedMessage(source, QStringLiteral("chat_1"),
                            QStringLiteral("m%1").arg(i),
                            QStringLiteral("body %1").arg(i), 100 + i));
    }

    ExportService exporter(&source);
    exporter.setKMac(kMac);
    QObject::connect(
        &exporter, &ExportService::progressChanged, &exporter,
        [&exporter](int percent, const QString &) {
            if (percent >= 15) {
                exporter.cancel();
            }
        },
        Qt::DirectConnection);

    ExportOptions options;
    options.outputPath = dir.filePath(QStringLiteral("cancelled.minibackup"));
    auto exported = waitFor<ExportSummary>([&](auto done) {
        exporter.exportBackup(options, done);
    });
    QVERIFY(exported.has_value());
    QVERIFY(exported->isOk());
    QVERIFY(exported->value().cancelled);
    QVERIFY(!QFileInfo::exists(options.outputPath));

    // A valid archive for the pre-cancelled import.
    auto valid = makeExport(source, dir.path(), QStringLiteral("valid.minibackup"),
                            false);
    QVERIFY(valid.has_value());

    storage::StorageWorker target;
    QVERIFY(target.open(dir.filePath(QStringLiteral("target.db")), dbKey()).isOk());
    const QString backups = dir.filePath(QStringLiteral("backups"));
    ImportService importer(&target);
    importer.setKMac(kMac);
    importer.setBackupLocation(backups, QString());
    ImportOptions importOptions;
    importOptions.inputPath = valid->path;
    importer.cancel();

    auto imported = waitFor<ImportSummary>([&](auto done) {
        importer.importBackup(importOptions, done);
    });
    QVERIFY(imported.has_value());
    QVERIFY(imported->isOk());
    QVERIFY(imported->value().cancelled);
    QCOMPARE(imported->value().messagesAdded, 0);
    QCOMPARE(messageCount(target, QStringLiteral("chat_1")), 0);
    QVERIFY(storage::Backup::list(backups).isEmpty());
}

QTEST_MAIN(TestExportPhase4)
#include "test_export_phase4.moc"
