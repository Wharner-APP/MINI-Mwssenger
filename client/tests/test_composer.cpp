#include "core/BuildInfo.h"
#include "ui/Composer.h"
#include "ui/Theme.h"

#include <QFontMetrics>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest>

using namespace mini;

class TestComposer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    }

    void enterSendsAndClears();
    void shiftEnterInsertsNewline();
    void sendButtonTracksInput();
    void growsUpToEightLines();
};

void TestComposer::enterSendsAndClears()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);
    composer.setText(QStringLiteral("Hello"));

    QSignalSpy spy(&composer, &Composer::sendRequested);
    QVERIFY(composer.sendButton()->isEnabled());
    QTest::keyClick(composer.editor(), Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("Hello"));
    QCOMPARE(composer.text(), QString());
}

void TestComposer::shiftEnterInsertsNewline()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);
    composer.setText(QStringLiteral("Line one"));
    composer.editor()->moveCursor(QTextCursor::End);

    QSignalSpy spy(&composer, &Composer::sendRequested);
    QTest::keyClick(composer.editor(), Qt::Key_Return, Qt::ShiftModifier);

    QCOMPARE(spy.count(), 0);
    QVERIFY(composer.text().startsWith(QStringLiteral("Line one")));
    QVERIFY(composer.text().size() > QStringLiteral("Line one").size());
    QVERIFY2(composer.text().contains(QLatin1Char('\n'))
                 || composer.text().contains(QChar(0x2028)),
             qPrintable(composer.text()));
}

void TestComposer::sendButtonTracksInput()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);

    QVERIFY(!composer.sendButton()->isEnabled());
    QVERIFY(!composer.canSend());
    composer.setText(QStringLiteral("   "));
    QVERIFY(!composer.sendButton()->isEnabled());
    composer.setText(QStringLiteral("text"));
    QVERIFY(composer.sendButton()->isEnabled());
    QVERIFY(composer.canSend());
}

void TestComposer::growsUpToEightLines()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);

    composer.setText(QStringLiteral("one"));
    QCoreApplication::processEvents();
    const int oneLine = composer.editor()->height();

    QStringList lines;
    for (int i = 0; i < 30; ++i) {
        lines.append(QStringLiteral("line %1").arg(i));
    }
    composer.setText(lines.join(QLatin1Char('\n')));
    QCoreApplication::processEvents();
    const int manyLines = composer.editor()->height();

    const int lineSpacing = QFontMetrics(composer.editor()->font()).lineSpacing();
    QVERIFY(manyLines > oneLine);
    QVERIFY2(manyLines <= lineSpacing * 8 + 22,
             qPrintable(QStringLiteral("height=%1 cap=%2").arg(manyLines).arg(lineSpacing * 8 + 22)));
}

QTEST_MAIN(TestComposer)
#include "test_composer.moc"
