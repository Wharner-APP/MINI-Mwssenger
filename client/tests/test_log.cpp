#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;

class TestLog : public QObject
{
    Q_OBJECT

private slots:
    void rotatesAndKeepsSevenFiles();
    void dropsForbiddenFields();
};

void TestLog::rotatesAndKeepsSevenFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Log::init(dir.path(), 1024, 7);
    Log::setMinLevel(Log::Level::Trace);

    for (int i = 0; i < 300; ++i) {
        Log::info(QStringLiteral("test.rotate"),
                  {{QStringLiteral("n"), i},
                   {QStringLiteral("payload"), QStringLiteral("abcdefghijklmnopqrstuvwxyz")}});
    }
    Log::trace(QStringLiteral("test.level"));
    Log::debug(QStringLiteral("test.level"));
    Log::warn(QStringLiteral("test.level"));
    Log::error(QStringLiteral("test.level"));
    Log::shutdown();

    const QStringList files =
        QDir(dir.path()).entryList({QStringLiteral("mini*.log")}, QDir::Files);
    QVERIFY2(files.size() <= 7, qPrintable(QStringLiteral("files=%1").arg(files.size())));
    QVERIFY2(files.size() >= 2, "rotation must have happened");

    int total = 0;
    for (const QString &name : files) {
        const qint64 size = QFileInfo(QDir(dir.path()).filePath(name)).size();
        QVERIFY2(size <= 2048, qPrintable(QStringLiteral("%1 size=%2").arg(name).arg(size)));
        ++total;
    }
    QCOMPARE(total, static_cast<int>(files.size()));
}

void TestLog::dropsForbiddenFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Log::init(dir.path(), 4096, 7);
    Log::setMinLevel(Log::Level::Trace);

    Log::info(QStringLiteral("test.redaction"),
              {{QStringLiteral("password"), QStringLiteral("hunter2")},
               {QStringLiteral("text"), QStringLiteral("private chat body")},
               {QStringLiteral("sign.in"), QStringLiteral("WORD1 WORD2")},
               {QStringLiteral("chat_id"), QStringLiteral("c_1")}});
    Log::shutdown();

    const QStringList files =
        QDir(dir.path()).entryList({QStringLiteral("mini*.log")}, QDir::Files);
    QVERIFY(!files.isEmpty());

    QString content;
    for (const QString &name : files) {
        QFile file(QDir(dir.path()).filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        content += QString::fromUtf8(file.readAll());
    }

    QVERIFY(content.contains(QStringLiteral("test.redaction")));
    QVERIFY(content.contains(QStringLiteral("chat_id=c_1")));
    QVERIFY(content.contains(QStringLiteral("redacted_fields=3")));
    QVERIFY(!content.contains(QStringLiteral("hunter2")));
    QVERIFY(!content.contains(QStringLiteral("private chat body")));
    QVERIFY(!content.contains(QStringLiteral("WORD1 WORD2")));
    QVERIFY(Log::isForbiddenField(QStringLiteral("password")));
    QVERIFY(!Log::isForbiddenField(QStringLiteral("chat_id")));
}

QTEST_MAIN(TestLog)
#include "test_log.moc"
