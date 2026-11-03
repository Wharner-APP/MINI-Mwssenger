#include "ui/MediaViewer.h"

#include <QImage>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace mini;

class TestMediaViewer : public QObject
{
    Q_OBJECT

private slots:
    void showsPhotoAndClampsZoom();
    void arrowsMoveThroughAlbum();
    void escapeClosesAndSignals();
    void videoItemUsesVideoPanel();
};

namespace {

QVector<MediaViewerItem> makeItems(const QString &dir)
{
    QVector<MediaViewerItem> items;
    for (int i = 0; i < 2; ++i) {
        QImage image(400 + i * 40, 300, QImage::Format_RGB32);
        image.fill(i == 0 ? QColor(200, 60, 60) : QColor(60, 120, 200));
        const QString path = dir + QStringLiteral("/photo%1.png").arg(i);
        image.save(path);
        MediaViewerItem item;
        item.mediaId = QStringLiteral("media-%1").arg(i);
        item.title = QStringLiteral("photo%1.png").arg(i);
        item.path = path;
        item.image = QPixmap::fromImage(image);
        item.kind = MediaKind::Photo;
        items.append(item);
    }
    return items;
}

} // namespace

void TestMediaViewer::showsPhotoAndClampsZoom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MediaViewer viewer;
    viewer.resize(800, 600);
    viewer.setItems(makeItems(dir.path()), 0);

    QCOMPARE(viewer.currentIndex(), 0);
    QCOMPARE(viewer.zoom(), 1.0);
    QVERIFY(!viewer.canvas()->image().isNull());

    viewer.canvas()->setZoom(50.0);
    QCOMPARE(viewer.canvas()->zoom(), 5.0); // §33.15: up to 500%
    viewer.canvas()->setZoom(0.001);
    QCOMPARE(viewer.canvas()->zoom(), 0.1); // and down to 10%
    viewer.canvas()->resetView();
    QCOMPARE(viewer.canvas()->zoom(), 1.0);
}

void TestMediaViewer::arrowsMoveThroughAlbum()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MediaViewer viewer;
    viewer.setItems(makeItems(dir.path()), 0);

    QVERIFY(viewer.nextButton()->isVisibleTo(&viewer));
    QVERIFY(viewer.previousButton()->isVisibleTo(&viewer));

    viewer.next();
    QCOMPARE(viewer.currentIndex(), 1);
    viewer.next();
    QCOMPARE(viewer.currentIndex(), 0); // wraps
    viewer.previous();
    QCOMPARE(viewer.currentIndex(), 1);

    QTest::keyClick(&viewer, Qt::Key_Left);
    QCOMPARE(viewer.currentIndex(), 0);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentIndex(), 1);

    viewer.setItems(makeItems(dir.path()).mid(0, 1), 0);
    QVERIFY(!viewer.nextButton()->isVisibleTo(&viewer));
}

void TestMediaViewer::escapeClosesAndSignals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MediaViewer viewer;
    viewer.setItems(makeItems(dir.path()), 0);
    viewer.show();
    QVERIFY(viewer.isVisible());

    QSignalSpy closed(&viewer, &MediaViewer::closed);
    QTest::keyClick(&viewer, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.isVisible(), 3000);
}

void TestMediaViewer::videoItemUsesVideoPanel()
{
    MediaViewer viewer;
    MediaViewerItem item;
    item.mediaId = QStringLiteral("video-1");
    item.title = QStringLiteral("clip.mp4");
    item.kind = MediaKind::Video;
    item.durationMs = 42000;
    viewer.setItems({item}, 0);

    QVERIFY(viewer.stack()->currentWidget() != viewer.canvas());
    // No local file yet: the external-player shortcut must stay disabled.
    auto *open = viewer.findChild<QPushButton *>(QStringLiteral("mediaViewerOpenExternal"));
    QVERIFY(open != nullptr);
    QVERIFY(!open->isEnabled());
}

QTEST_MAIN(TestMediaViewer)
#include "test_media_viewer.moc"
