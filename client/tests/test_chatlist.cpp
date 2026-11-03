#include "core/BuildInfo.h"
#include "ui/ChatItemDelegate.h"
#include "ui/ChatList.h"
#include "ui/ChatListModel.h"
#include "ui/Theme.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QListView>
#include <QScrollBar>
#include <QtTest>

using namespace mini;

namespace {

class PaintCounter : public QObject
{
public:
    int frames = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) {
            ++frames;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

class TestChatList : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
    }

    void holdsTenThousandStubs();
    void staysVirtualised();
    void scrollsAtFrameBudget();
    void avatarInitialsMeetContrastInBothThemes();
};

namespace {
QColor blend(const QColor &foreground, const QColor &background)
{
    QColor tint = foreground;
    tint.setAlpha(52);
    const qreal alpha = tint.alphaF();
    return QColor::fromRgbF(
        tint.redF() * alpha + background.redF() * (1.0 - alpha),
        tint.greenF() * alpha + background.greenF() * (1.0 - alpha),
        tint.blueF() * alpha + background.blueF() * (1.0 - alpha));
}
} // namespace

void TestChatList::holdsTenThousandStubs()
{
    ChatList list;
    list.resize(320, 640);
    list.show();
    list.setStubCount(10000);

    QCOMPARE(list.model()->rowCount(), 10000);
    QCOMPARE(list.model()->totalCount(), 10000);
    QVERIFY(!list.isEmpty());
}

void TestChatList::staysVirtualised()
{
    ChatList list;
    list.resize(320, 640);
    list.show();
    list.setStubCount(10000);
    QListView *view = list.view();
    QCoreApplication::processEvents();
    QCOMPARE(view->visualRect(view->model()->index(0, 0)).height(),
             Theme::instance().rowHeight());

    view->scrollToBottom();
    QCoreApplication::processEvents();

    QVERIFY2(view->viewport()->findChildren<QWidget *>().size() < 50,
             "QListView must not create a widget per row");
    QCOMPARE(view->verticalScrollBar()->value(), view->verticalScrollBar()->maximum());
}

void TestChatList::scrollsAtFrameBudget()
{
    ChatList list;
    list.resize(320, 640);
    list.show();
    list.setStubCount(10000);
    QListView *view = list.view();
    view->scrollToTop();
    QCoreApplication::processEvents();

    QScrollBar *bar = view->verticalScrollBar();
    const int span = bar->maximum() - bar->minimum();
    QVERIFY(span > 1000);

    // Count actual viewport paints (frames) while the list scrolls through
    // all 10 000 rows. The earlier version counted loop iterations, which is
    // not a frame rate.
    PaintCounter counter;
    view->viewport()->installEventFilter(&counter);

    int position = bar->minimum();
    const int step = qMax(1, span / 500);
    bool reachedBottom = false;

    // Deterministic drive: each iteration advances the scrollbar and forces
    // one synchronous paint, which the counter records as a frame. The loop
    // runs for at least a second and cannot finish before the list has been
    // scrolled through to the last stub.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 || !reachedBottom) {
        position += step;
        if (position >= bar->maximum()) {
            position = bar->maximum();
            reachedBottom = true;
        }
        bar->setValue(position);
        view->viewport()->repaint();
        if (position == bar->maximum()) {
            position = bar->minimum();
        }
    }
    const qint64 elapsedMs = qMax<qint64>(1, timer.elapsed());

    view->viewport()->removeEventFilter(&counter);
    const double fps = counter.frames * 1000.0 / elapsedMs;

    QVERIFY(reachedBottom);
    QVERIFY(counter.frames > 0);
    qInfo() << "frames" << counter.frames << "elapsed_ms" << elapsedMs << "fps" << fps;
    QVERIFY2(fps >= 55.0,
             qPrintable(QStringLiteral("frames=%1 elapsed_ms=%2 fps=%3")
                            .arg(counter.frames)
                            .arg(elapsedMs)
                            .arg(fps, 0, 'f', 1)));
}

void TestChatList::avatarInitialsMeetContrastInBothThemes()
{
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
        Theme::instance().apply(mode, Theme::Accent::Lime, 100, Density::Comfortable);
        const Tokens tokens = Theme::instance().tokens();
        for (const AccentDef &accent : Theme::accents()) {
            const QColor tinted = blend(accent.color, tokens.bgList);
            const QColor foreground =
                ChatItemDelegate::initialsColor(accent.color, tokens.bgList);
            const double ratio = Theme::contrastRatio(foreground, tinted);
            QVERIFY2(ratio >= 4.5,
                     qPrintable(QStringLiteral("%1/%2 ratio=%3")
                                    .arg(Theme::modeId(mode), accent.id)
                                    .arg(ratio, 0, 'f', 2)));
        }
    }
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100, Density::Comfortable);
}

QTEST_MAIN(TestChatList)
#include "test_chatlist.moc"
