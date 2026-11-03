#include "core/BuildInfo.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

using namespace mini;

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    }

    void textContrastMeetsWcag();
    void bundledFontsAreLoaded();
    void styleSheetCarriesFontSizes();
    void eightAccentsAreDistinct();
    void fontScaleAppliesImmediately();
    void densityChangesRowHeight();
    void changeEmitsSignal();
};

void TestTheme::textContrastMeetsWcag()
{
    for (const ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::Amoled}) {
        Theme::instance().apply(mode, Theme::Accent::Lime, 100, Density::Comfortable);
        const Tokens t = Theme::instance().tokens();
        const auto check = [mode](const QColor &foreground, const QColor &background,
                                  const char *label) {
            const double ratio = Theme::contrastRatio(foreground, background);
            QVERIFY2(ratio >= 4.5,
                     qPrintable(QStringLiteral("%1 %2 contrast=%3")
                                    .arg(Theme::modeId(mode), QString::fromLatin1(label))
                                    .arg(ratio)));
        };
        check(t.textPrimary, t.bgBase, "primary/base");
        check(t.textSecondary, t.bgBase, "secondary/base");
        check(t.textTertiary, t.bgBase, "tertiary/base");
        check(t.textPrimary, t.bgList, "primary/list");
        check(t.textPrimary, t.bgChat, "primary/chat");
        check(t.accentText, t.accent, "accentText/accent");
    }
}

void TestTheme::bundledFontsAreLoaded()
{
    const QStringList families = QFontDatabase::families();
    QVERIFY2(families.contains(QStringLiteral("Inter")),
             qPrintable(families.join(QLatin1Char(','))));
    QVERIFY(families.contains(QStringLiteral("Manrope")));
}

void TestTheme::styleSheetCarriesFontSizes()
{
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    const QString qss = Theme::instance().styleSheet();

    // Qt style sheets require "22px" with no space; a dropped declaration
    // would leave every heading and caption at the default size.
    QVERIFY2(qss.contains(QStringLiteral("font-size: 22px;")), qPrintable(qss));
    QVERIFY2(qss.contains(QStringLiteral("font-size: 16px;")), qPrintable(qss));
    QVERIFY2(qss.contains(QStringLiteral("font-size: 12px;")), qPrintable(qss));
    QVERIFY(!qss.contains(QStringLiteral(" px;")));
}

void TestTheme::eightAccentsAreDistinct()
{
    const QVector<AccentDef> accents = Theme::accents();
    QCOMPARE(static_cast<int>(accents.size()), 8);

    QSet<QString> colors;
    for (const AccentDef &def : accents) {
        colors.insert(def.color.name());
    }
    QCOMPARE(static_cast<int>(colors.size()), 8);

    for (const AccentDef &def : accents) {
        Theme::instance().apply(ThemeMode::Dark, Theme::accentFromId(def.id), 100,
                                Density::Comfortable);
        QVERIFY2(Theme::instance().styleSheet().contains(def.color.name()),
                 qPrintable(def.id));
    }
}

void TestTheme::fontScaleAppliesImmediately()
{
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 90, Density::Comfortable);
    const qreal small = qApp->font().pointSizeF();
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 112, Density::Comfortable);
    const qreal large = qApp->font().pointSizeF();

    QVERIFY(large > small);
    QCOMPARE(Theme::instance().scaledPx(100), 112);
}

void TestTheme::densityChangesRowHeight()
{
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    QCOMPARE(Theme::instance().rowHeight(), 68);
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Compact);
    QCOMPARE(Theme::instance().rowHeight(), 56);
}

void TestTheme::changeEmitsSignal()
{
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    QSignalSpy spy(&Theme::instance(), &Theme::changed);
    Theme::instance().apply(ThemeMode::Amoled, Theme::Accent::Mint, 112, Density::Compact);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
