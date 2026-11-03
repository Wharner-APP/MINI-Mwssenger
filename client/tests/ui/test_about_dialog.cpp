#include "core/BuildInfo.h"
#include "ui/AboutDialog.h"
#include "ui/Theme.h"

#include <QLabel>
#include <QtTest>

using namespace mini;

class TestAboutDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }
    void showsVersionLicensesAndDisclaimer();
};

void TestAboutDialog::showsVersionLicensesAndDisclaimer()
{
    AboutDialog dialog;

    QVERIFY(dialog.versionLabel()->text().contains(BuildInfo::appVersion()));
    const QString licenses = dialog.licensesLabel()->text();
    QVERIFY(licenses.contains(QStringLiteral("Qt")));
    QVERIFY(licenses.contains(QStringLiteral("libsodium")));
    QVERIFY(licenses.contains(QStringLiteral("SQLite")));
    QVERIFY(dialog.disclaimerLabel()->text().contains(
        QStringLiteral("not affiliated with MINI/BMW")));
}

QTEST_MAIN(TestAboutDialog)
#include "test_about_dialog.moc"
