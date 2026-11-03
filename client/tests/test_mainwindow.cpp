#include "core/BuildInfo.h"
#include "ui/ChatList.h"
#include "ui/ChatView.h"
#include "ui/Composer.h"
#include "ui/MainWindow.h"
#include "ui/Rail.h"
#include "ui/Sidebar.h"
#include "ui/Theme.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Icons.h"

#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest>

using namespace mini;

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
        QVERIFY(Icons::available().contains(QStringLiteral("chats")));
        QVERIFY(!Icons::pixmap(QStringLiteral("chats"), QColor(255, 255, 255), 24).isNull());
    }

    void layoutMatchesSpec();
    void emptyStateExplainsNextStep();
    void composerStartsDisabled();
    void keyboardFocusIsReachable();
    void globalShortcutsAreRegistered();
};

void TestMainWindow::layoutMatchesSpec()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    QCOMPARE(window.minimumWidth(), 1000);
    QCOMPARE(window.minimumHeight(), 640);
    QCOMPARE(window.rail()->width(), 72);
    QVERIFY(window.sidebar()->width() >= 260);
    QVERIFY(window.sidebar()->width() <= 420);
    QCOMPARE(window.chatView()->minimumWidth(), 560);
    QVERIFY(window.chatView()->width() >= 560);
}

void TestMainWindow::emptyStateExplainsNextStep()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    ChatList *list = window.sidebar()->chatList();
    QVERIFY(list->isEmpty());
    EmptyState *empty = list->emptyState();
    QVERIFY(empty->isVisible());
    QVERIFY(!empty->title().isEmpty());
    QVERIFY(!empty->description().isEmpty());
    QVERIFY(empty->actionButton()->isVisible());
    QVERIFY(!empty->actionButton()->text().isEmpty());
    QCOMPARE(empty->actionButton()->text(), QStringLiteral("New chat"));
}

void TestMainWindow::composerStartsDisabled()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    Composer *composer = window.chatView()->composer();
    QVERIFY(!composer->editor()->isEnabled());
    QVERIFY(!composer->sendButton()->isEnabled());

    window.chatView()->setChatTitle(QStringLiteral("Test chat"));
    QVERIFY(composer->editor()->isEnabled());
}

void TestMainWindow::keyboardFocusIsReachable()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    QCOMPARE(window.rail()->chatsButton()->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(window.rail()->settingsButton()->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(window.sidebar()->newChatButton()->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(window.sidebar()->searchField()->focusPolicy(), Qt::StrongFocus);
}

void TestMainWindow::globalShortcutsAreRegistered()
{
    MainWindow window;
    QStringList sequences;
    for (QShortcut *shortcut : window.findChildren<QShortcut *>()) {
        sequences.append(shortcut->key().toString(QKeySequence::PortableText));
    }
    for (const QString &expected :
         {QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+,"), QStringLiteral("F5"),
          QStringLiteral("F11"), QStringLiteral("Ctrl+Q")}) {
        QVERIFY2(sequences.contains(expected), qPrintable(expected));
    }
}

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
