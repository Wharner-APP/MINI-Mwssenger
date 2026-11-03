#include "core/BuildInfo.h"
#include "core/Settings.h"
#include "storage/StorageWorker.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "ui/widgets/ColorSwatch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;

namespace {
QByteArray dbKey()
{
    return QByteArray(32, '\x5a');
}
} // namespace

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }
    void init()
    {
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void appearanceAppliesImmediatelyAndPersists();
    void closeToTrayDefaultsOnAndPersists();
    void backupButtonRunsStorageBackup();
};

void TestSettingsDialog::appearanceAppliesImmediatelyAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    Settings settings(path);
    SettingsDialog dialog(&settings);

    const int lightIndex = dialog.themeCombo()->findData(Theme::modeId(ThemeMode::Light));
    QVERIFY(lightIndex >= 0);
    dialog.themeCombo()->setCurrentIndex(lightIndex);
    QCOMPARE(Theme::instance().mode(), ThemeMode::Light);

    QVERIFY(dialog.accentSwatch(2) != nullptr);
    dialog.accentSwatch(2)->click();
    QCOMPARE(Theme::accentId(Theme::instance().accent()),
             Theme::accents().at(2).id);

    const int scaleIndex = dialog.scaleCombo()->findData(112);
    QVERIFY(scaleIndex >= 0);
    dialog.scaleCombo()->setCurrentIndex(scaleIndex);
    QCOMPARE(Theme::instance().fontScalePercent(), 112);

    const int compactIndex = dialog.densityCombo()->findData(Theme::densityId(Density::Compact));
    QVERIFY(compactIndex >= 0);
    dialog.densityCombo()->setCurrentIndex(compactIndex);
    QCOMPARE(Theme::instance().density(), Density::Compact);
    QCOMPARE(Theme::instance().rowHeight(), 56);

    Settings reopened(path);
    QCOMPARE(reopened.get(QStringLiteral("ui/theme")), QStringLiteral("light"));
    QCOMPARE(reopened.getInt(QStringLiteral("ui/font_scale"), 100), 112);
    QCOMPARE(reopened.get(QStringLiteral("ui/density")), QStringLiteral("compact"));
}

void TestSettingsDialog::closeToTrayDefaultsOnAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(path);
        SettingsDialog dialog(&settings);
        QVERIFY(dialog.closeToTrayCheckBox()->isChecked());
        dialog.closeToTrayCheckBox()->setChecked(false);
        QCOMPARE(settings.getBool(QStringLiteral("ui/close_to_tray"), true), false);
    }

    Settings reopened(path);
    SettingsDialog again(&reopened);
    QVERIFY(!again.closeToTrayCheckBox()->isChecked());
}

void TestSettingsDialog::backupButtonRunsStorageBackup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString backups = dir.filePath(QStringLiteral("backups"));
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey(), backups).isOk());

    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&settings);
    dialog.setBackupTarget(&worker, dir.filePath(QStringLiteral("keys.bin")), backups);
    QVERIFY(dialog.backupButton()->isEnabled());

    dialog.backupButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        !QDir(backups).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(), 5000);
}

QTEST_MAIN(TestSettingsDialog)
#include "test_settings_dialog.moc"
