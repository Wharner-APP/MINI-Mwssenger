#include "core/Settings.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsAcrossInstances();
    void missingKeysUseFallback();
    void storesValuesWithSeparators();
};

void TestSettings::roundTripsAcrossInstances()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(path);
        settings.set(QStringLiteral("ui/theme"), QStringLiteral("amoled"));
        settings.set(QStringLiteral("ui/accent"), QStringLiteral("mint"));
        settings.setInt(QStringLiteral("ui/font_scale"), 112);
        settings.set(QStringLiteral("ui/density"), QStringLiteral("compact"));
        settings.setBool(QStringLiteral("window/maximized"), true);
        settings.flush();
    }

    QVERIFY(QFileInfo::exists(path));

    Settings reopened(path);
    QCOMPARE(reopened.get(QStringLiteral("ui/theme")), QStringLiteral("amoled"));
    QCOMPARE(reopened.get(QStringLiteral("ui/accent")), QStringLiteral("mint"));
    QCOMPARE(reopened.getInt(QStringLiteral("ui/font_scale"), 100), 112);
    QCOMPARE(reopened.get(QStringLiteral("ui/density")), QStringLiteral("compact"));
    QCOMPARE(reopened.getBool(QStringLiteral("window/maximized"), false), true);
}

void TestSettings::missingKeysUseFallback()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    QCOMPARE(settings.get(QStringLiteral("ui/theme"), QStringLiteral("dark")),
             QStringLiteral("dark"));
    QCOMPARE(settings.getInt(QStringLiteral("ui/font_scale"), 100), 100);
    QCOMPARE(settings.getBool(QStringLiteral("window/maximized"), false), false);
}

void TestSettings::storesValuesWithSeparators()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    const QString tricky = QStringLiteral("line one; line two = value, comma");
    {
        Settings settings(path);
        settings.set(QStringLiteral("test/tricky"), tricky);
        settings.flush();
    }
    Settings reopened(path);
    QCOMPARE(reopened.get(QStringLiteral("test/tricky")), tricky);
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
