#include "core/BuildInfo.h"
#include "services/EmojiCatalog.h"
#include "services/GifCatalog.h"
#include "services/StickerService.h"
#include "ui/Composer.h"
#include "ui/EmojiPanel.h"
#include "ui/MessageBubble.h"
#include "ui/GifPanel.h"
#include "ui/StickerPanel.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest>

using namespace mini;
using namespace mini::services;

class TestPickerUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void emojiPanelHasEightCategories();
    void emojiSearchAndRecent();
    void composerInsertsEmojiAtCursor();
    void stickerPanelListsPacksAndEmits();
    void gifPanelHasCatalog();
    void composerButtonsTogglePickers();
    void bubbleDelegateRendersRichMessages();
};

void TestPickerUi::emojiPanelHasEightCategories()
{
    EmojiPanel panel;
    QCOMPARE(EmojiCatalog::instance().categories().size(), 8);
    // Recent + eight categories.
    QCOMPARE(panel.categoryBox()->count(), 9);
    panel.categoryBox()->setCurrentIndex(1);
    QVERIFY(panel.itemCount() > 0);
}

void TestPickerUi::emojiSearchAndRecent()
{
    EmojiPanel panel;
    panel.show();
    panel.searchEdit()->setText(QStringLiteral("улыбка"));
    QVERIFY(panel.itemCount() > 0);

    QSignalSpy selected(&panel, &EmojiPanel::emojiSelected);
    QSignalSpy recentChanged(&panel, &EmojiPanel::recentChanged);
    QListWidgetItem *item = panel.grid()->item(0);
    QVERIFY(item);
    const QString character = item->data(Qt::UserRole).toString();
    QVERIFY(!character.isEmpty());
    const QRect rect = panel.grid()->visualItemRect(item);
    QTest::mouseClick(panel.grid()->viewport(), Qt::LeftButton, {}, rect.center());
    QTRY_COMPARE(selected.count(), 1);
    QCOMPARE(selected.first().first().toString(), character);
    QVERIFY(recentChanged.count() >= 1);

    panel.searchEdit()->clear();
    panel.categoryBox()->setCurrentIndex(0);
    QVERIFY(panel.itemCount() >= 1);
    QCOMPARE(panel.grid()->item(0)->data(Qt::UserRole).toString(), character);
}

void TestPickerUi::composerInsertsEmojiAtCursor()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);
    composer.setText(QStringLiteral("ab"));
    QTextCursor cursor = composer.editor()->textCursor();
    cursor.setPosition(1);
    composer.editor()->setTextCursor(cursor);

    composer.insertEmoji(QStringLiteral("😀"));
    QCOMPARE(composer.text(), QStringLiteral("a😀b"));
}

void TestPickerUi::stickerPanelListsPacksAndEmits()
{
    StickerService stickers;
    StickerPanel panel;
    panel.setService(&stickers);
    panel.show();

    QVERIFY(panel.packBox()->count() >= 3);
    QVERIFY(panel.grid()->count() >= 8);
    QVERIFY(!panel.currentPackId().isEmpty());

    QSignalSpy selected(&panel, &StickerPanel::stickerSelected);
    QListWidgetItem *item = panel.grid()->item(0);
    QVERIFY(item);
    const QRect rect = panel.grid()->visualItemRect(item);
    QTest::mouseClick(panel.grid()->viewport(), Qt::LeftButton, {}, rect.center());
    QTRY_COMPARE(selected.count(), 1);
    QCOMPARE(selected.first().at(0).toString(), panel.currentPackId());
    QCOMPARE(selected.first().at(1).toString(), item->data(Qt::UserRole).toString());
}

void TestPickerUi::gifPanelHasCatalog()
{
    GifPanel panel;
    panel.show();
    QVERIFY2(GifCatalog::instance().items().size() >= 12,
             qPrintable(QString::number(GifCatalog::instance().items().size())));
    QVERIFY(panel.itemCount() >= 12);
    QVERIFY(panel.categoryBox()->count() >= 2);

    QSignalSpy selected(&panel, &GifPanel::gifSelected);
    auto *button = panel.gridHost()->findChild<QPushButton *>();
    QVERIFY(button);
    button->click();
    QCOMPARE(selected.count(), 1);
}

void TestPickerUi::composerButtonsTogglePickers()
{
    Composer composer;
    composer.show();
    composer.setComposerEnabled(true);

    QVERIFY(!composer.picker()->isVisible());
    composer.emojiButton()->click();
    QVERIFY(composer.picker()->isVisible());
    QVERIFY(composer.emojiButton()->isChecked());

    composer.stickerButton()->click();
    QVERIFY(composer.picker()->isVisible());
    QVERIFY(composer.stickerButton()->isChecked());
    QVERIFY(!composer.emojiButton()->isChecked());

    composer.stickerButton()->click();
    QVERIFY(!composer.picker()->isVisible());
}

void TestPickerUi::bubbleDelegateRendersRichMessages()
{
    StickerService stickers;
    MessageListModel model;
    model.setStickerService(&stickers);

    Message sticker;
    sticker.clientMsgId = QStringLiteral("sticker");
    sticker.chatId = QStringLiteral("chat");
    sticker.kind = MediaKind::Sticker;
    sticker.mediaId = QStringLiteral("mini.cats/cats-happy");
    sticker.body = QStringLiteral("Радость");
    sticker.localTs = 1000;

    Message gif;
    gif.clientMsgId = QStringLiteral("gif");
    gif.chatId = QStringLiteral("chat");
    gif.kind = MediaKind::Gif;
    gif.mediaId = QStringLiteral("bounce");
    gif.body = QStringLiteral("Прыжок");
    gif.localTs = 2000;

    Message album;
    album.clientMsgId = QStringLiteral("album");
    album.chatId = QStringLiteral("chat");
    album.kind = MediaKind::Album;
    album.mediaId = QStringLiteral("up_1,up_2,up_3");
    album.body = QStringLiteral("подпись альбома");
    album.localTs = 3000;

    model.setMessages({sticker, gif, album});
    QCOMPARE(model.rowCount(), 3);
    const QModelIndex stickerIndex = model.index(0, 0);
    QVERIFY(!stickerIndex.data(MessageListModel::StickerPixmapRole).value<QPixmap>().isNull());
    QCOMPARE(stickerIndex.data(MessageListModel::StickerPackRole).toString(),
             QStringLiteral("mini.cats"));
    QCOMPARE(stickerIndex.data(MessageListModel::StickerIdRole).toString(),
             QStringLiteral("cats-happy"));
    const QModelIndex gifIndex = model.index(1, 0);
    QVERIFY(!gifIndex.data(MessageListModel::GifFrameRole).value<QPixmap>().isNull());
    const QModelIndex albumIndex = model.index(2, 0);
    QCOMPARE(albumIndex.data(MessageListModel::AlbumIdsRole).toStringList().size(), 3);

    QWidget host;
    host.resize(640, 900);
    MessageBubbleDelegate delegate;
    QPixmap canvas(640, 900);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    int top = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        QStyleOptionViewItem option;
        option.widget = &host;
        option.rect = QRect(0, top, 640, 220);
        delegate.paint(&painter, option, model.index(row, 0));
        const QSize hint = delegate.sizeHint(option, model.index(row, 0));
        QVERIFY(hint.height() > 40);
        top += hint.height();
    }
    painter.end();
    QVERIFY(top > 300);
}

QTEST_MAIN(TestPickerUi)
#include "test_picker_ui.moc"
