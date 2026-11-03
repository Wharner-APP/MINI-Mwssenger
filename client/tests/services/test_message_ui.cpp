#include "core/BuildInfo.h"
#include "services/ChatService.h"
#include "services/SearchService.h"
#include "storage/ChatsRepo.h"
#include "storage/StorageWorker.h"
#include "ui/ChatList.h"
#include "ui/ChatListModel.h"
#include "ui/ChatView.h"
#include "ui/Composer.h"
#include "ui/MessageBubble.h"
#include "ui/Theme.h"
#include "ui/widgets/EmptyState.h"

#include "ServiceTestSupport.h"

#include <QAction>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QWidget>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

class TestMessageUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void chatListShowsPreviewUnreadPinAndMute();
    void chatListFiltersUnreadAndPinned();
    void presenceUpdatesTheChatRow();
    void chatMenuHasEightActions();
    void chatMenuActionsEmitState();
    void keyboardShortcutsAreRegistered();
    void ctrlTabSelectsNextChat();
    void chatViewRendersMessagesAndStatuses();
    void composerSendsWithReply();
    void draftSignalAndDraftText();
    void messageListStaysVirtualisedAndScrolls();
    void ctrlKSearchFindsChatAndMessage();
    void chatSearchJumpsAndHighlightsMatches();
    void jumpToMessageHighlightsTheRow();
    void multiSelectCollectsIdsAndEscapes();
    void pinnedBannerShowsAndHides();
    void formattedMessageExposesEditedAndForwardedRoles();
};

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

Chat makeChat(const QString &id, const QString &title, int unread, bool pinned, bool muted,
              qint64 ts = 1000)
{
    Chat chat;
    chat.chatId = id;
    chat.peerLogin = title.toLower();
    chat.title = title;
    chat.lastMessageTs = ts;
    chat.lastMessagePreview = QStringLiteral("last message");
    chat.unreadCount = unread;
    chat.pinned = pinned;
    chat.muted = muted;
    return chat;
}

Message makeMessage(const QString &id, const QString &body, bool outgoing, MessageState state,
                    qint64 ts)
{
    Message message;
    message.clientMsgId = id;
    message.chatId = QStringLiteral("chat");
    message.body = body;
    message.outgoing = outgoing;
    message.state = state;
    message.localTs = ts;
    return message;
}

} // namespace

void TestMessageUi::chatListShowsPreviewUnreadPinAndMute()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 3, true, false),
                   makeChat(QStringLiteral("b"), QStringLiteral("Bob"), 0, false, true)});

    QCOMPARE(list.model()->rowCount(), 2);
    const ChatItem first = list.model()->itemAt(0);
    QCOMPARE(first.id, QStringLiteral("a"));
    QCOMPARE(first.preview, QStringLiteral("last message"));
    QCOMPARE(first.unread, 3);
    QVERIFY(first.pinned);
    QVERIFY(!first.time.isEmpty());
    QVERIFY(list.model()->itemAt(1).muted);
    QVERIFY(!list.isEmpty());
}

void TestMessageUi::chatListFiltersUnreadAndPinned()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 3, true, false),
                   makeChat(QStringLiteral("b"), QStringLiteral("Bob"), 0, false, false)});

    list.setFilterMode(ChatList::FilterMode::Unread);
    QCOMPARE(list.model()->rowCount(), 1);
    QCOMPARE(list.model()->itemAt(0).id, QStringLiteral("a"));

    list.setFilterMode(ChatList::FilterMode::Pinned);
    QCOMPARE(list.model()->rowCount(), 1);
    QCOMPARE(list.model()->itemAt(0).id, QStringLiteral("a"));

    list.setFilterMode(ChatList::FilterMode::All);
    QCOMPARE(list.model()->rowCount(), 2);
}

void TestMessageUi::presenceUpdatesTheChatRow()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 0, false, false)});
    QVERIFY(!list.model()->itemAt(0).online);
    list.setPresence(QStringLiteral("alice"), true, 1234);
    QVERIFY(list.model()->itemAt(0).online);
    QCOMPARE(list.presenceFor(QStringLiteral("ALICE")).second, qint64(1234));
    list.setPresence(QStringLiteral("alice"), false, 5678);
    QVERIFY(!list.model()->itemAt(0).online);
}

void TestMessageUi::chatMenuHasEightActions()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 0, false, false)});

    QScopedPointer<QMenu> menu(list.buildChatMenu(QStringLiteral("a")));
    QCOMPARE(menu->actions().size(), 9);
    const QStringList expected{
        QStringLiteral("chatOpenAction"),      QStringLiteral("chatMarkReadAction"),
        QStringLiteral("chatPinAction"),       QStringLiteral("chatArchiveAction"),
        QStringLiteral("chatMuteAction"),      QStringLiteral("chatCopyLoginAction"),
        QStringLiteral("chatCopyTitleAction"), QStringLiteral("chatClearHistoryAction"),
        QStringLiteral("chatDeleteAction")};
    QStringList actual;
    for (QAction *action : menu->actions()) {
        actual.append(action->objectName());
    }
    QCOMPARE(actual, expected);
}

void TestMessageUi::chatMenuActionsEmitState()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 0, false, false)});
    QScopedPointer<QMenu> menu(list.buildChatMenu(QStringLiteral("a")));

    QSignalSpy pinned(&list, &ChatList::pinToggled);
    QSignalSpy mute(&list, &ChatList::muteUntilRequested);
    QSignalSpy archived(&list, &ChatList::archiveToggled);
    QSignalSpy read(&list, &ChatList::markReadRequested);

    menu->actions().at(2)->trigger();
    menu->actions().at(3)->trigger();
    QAction *muteAction = nullptr;
    for (QAction *action : menu->actions()) {
        if (action->objectName() == QLatin1String("chatMuteAction")) {
            muteAction = action;
            break;
        }
    }
    QVERIFY(muteAction);
    QVERIFY(muteAction->menu());
    QAction *muteForever = nullptr;
    for (QAction *action : muteAction->menu()->actions()) {
        if (action->text().contains(QLatin1String("Forever"))) {
            muteForever = action;
            break;
        }
    }
    QVERIFY(muteForever);
    muteForever->trigger();
    menu->actions().at(1)->trigger();

    QCOMPARE(pinned.count(), 1);
    QCOMPARE(pinned.first().at(0).toString(), QStringLiteral("a"));
    QCOMPARE(pinned.first().at(1).toBool(), true);
    QCOMPARE(archived.count(), 1);
    QCOMPARE(archived.first().at(1).toBool(), true);
    QCOMPARE(mute.count(), 1);
    QCOMPARE(mute.first().at(0).toString(), QStringLiteral("a"));
    QCOMPARE(mute.first().at(1).toLongLong(), qint64(-1));
    QCOMPARE(read.count(), 1);
}

void TestMessageUi::keyboardShortcutsAreRegistered()
{
    ChatList list;
    bool hasSearch = false;
    bool hasTab = false;
    bool hasBackTab = false;
    for (QShortcut *shortcut : list.findChildren<QShortcut *>()) {
        const QKeySequence sequence = shortcut->key();
        if (sequence == QKeySequence(QStringLiteral("Ctrl+K"))) {
            hasSearch = true;
        }
        if (sequence == QKeySequence(Qt::CTRL | Qt::Key_Tab)) {
            hasTab = true;
        }
        if (sequence == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)) {
            hasBackTab = true;
        }
    }
    QVERIFY(hasSearch);
    QVERIFY(hasTab);
    QVERIFY(hasBackTab);
}

void TestMessageUi::ctrlTabSelectsNextChat()
{
    ChatList list;
    list.setChats({makeChat(QStringLiteral("a"), QStringLiteral("Alice"), 0, false, false),
                   makeChat(QStringLiteral("b"), QStringLiteral("Bob"), 0, false, false)});
    QSignalSpy selected(&list, &ChatList::chatSelected);

    list.selectNext();
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.first().first().toString(), QStringLiteral("a"));

    list.selectNext();
    QCOMPARE(selected.count(), 2);
    QCOMPARE(selected.at(1).first().toString(), QStringLiteral("b"));

    list.selectPrevious();
    QCOMPARE(selected.at(2).first().toString(), QStringLiteral("a"));
}

void TestMessageUi::chatViewRendersMessagesAndStatuses()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.show();
    QCoreApplication::processEvents();

    Message incoming =
        makeMessage(QStringLiteral("m1"), QStringLiteral("hello"), false, MessageState::Received,
                    100);
    Message outgoing =
        makeMessage(QStringLiteral("m2"), QStringLiteral("hi"), true, MessageState::Queued, 200);
    view.setMessages({incoming, outgoing});

    QCOMPARE(view.messageModel()->count(), 2);
    QCOMPARE(view.messageModel()->messageAt(1).clientMsgId, QStringLiteral("m2"));
    QCOMPARE(view.messageModel()->data(view.messageModel()->index(1, 0),
                                       MessageListModel::StateRole).toInt(),
             static_cast<int>(MessageState::Queued));

    Message delivered = outgoing;
    delivered.state = MessageState::Delivered;
    view.updateMessage(delivered);
    QCOMPARE(view.messageModel()->data(view.messageModel()->index(1, 0),
                                       MessageListModel::StateRole).toInt(),
             static_cast<int>(MessageState::Delivered));

    view.removeMessage(QStringLiteral("m1"));
    QCOMPARE(view.messageModel()->count(), 1);
}

void TestMessageUi::composerSendsWithReply()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.show();
    QCoreApplication::processEvents();

    view.setReplyTo(QStringLiteral("m1"), QStringLiteral("quoted text"));
    QVERIFY(view.composer()->replyBar()->isVisible());
    QCOMPARE(view.replyToClientMsgId(), QStringLiteral("m1"));

    QSignalSpy sent(&view, &ChatView::sendRequested);
    view.composer()->setText(QStringLiteral("answer"));
    QTest::keyClick(view.composer()->editor(), Qt::Key_Return);

    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QStringLiteral("answer"));
    QVERIFY(!view.composer()->hasReplyPreview());
}

void TestMessageUi::draftSignalAndDraftText()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.setDraft(QStringLiteral("saved draft"));
    QCOMPARE(view.draft(), QStringLiteral("saved draft"));

    QSignalSpy drafts(&view, &ChatView::draftChanged);
    view.composer()->editor()->clear();
    view.composer()->editor()->insertPlainText(QStringLiteral("new text"));
    QTRY_COMPARE_WITH_TIMEOUT(drafts.count() >= 1, true, 2000);
    QCOMPARE(drafts.last().first().toString(), QStringLiteral("new text"));
}

void TestMessageUi::messageListStaysVirtualisedAndScrolls()
{
    ChatView view;
    view.resize(800, 640);
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));

    QVector<Message> messages;
    messages.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        messages.append(makeMessage(QStringLiteral("m%1").arg(i),
                                    QStringLiteral("message %1").arg(i), i % 2 == 0,
                                    i % 2 == 0 ? MessageState::Read : MessageState::Received,
                                    1000 + i));
    }
    view.setMessages(messages);
    view.show();
    QCoreApplication::processEvents();

    QListView *list = view.messageView();
    QCOMPARE(view.messageModel()->count(), 10000);
    QVERIFY2(list->viewport()->findChildren<QWidget *>().size() < 50,
             "the history must stay virtualised");

    QScrollBar *bar = list->verticalScrollBar();
    QVERIFY(bar->maximum() - bar->minimum() > 1000);

    PaintCounter counter;
    list->viewport()->installEventFilter(&counter);
    QElapsedTimer timer;
    timer.start();
    int position = bar->minimum();
    const int step = qMax(1, (bar->maximum() - bar->minimum()) / 500);
    bool reachedBottom = false;
    while (timer.elapsed() < 1000 || !reachedBottom) {
        position += step;
        if (position >= bar->maximum()) {
            position = bar->maximum();
            reachedBottom = true;
        }
        bar->setValue(position);
        list->viewport()->repaint();
        if (position == bar->maximum()) {
            position = bar->minimum();
        }
    }
    const qint64 elapsedMs = qMax<qint64>(1, timer.elapsed());
    list->viewport()->removeEventFilter(&counter);

    const double fps = counter.frames * 1000.0 / elapsedMs;
    QVERIFY(reachedBottom);
    QVERIFY2(fps >= 55.0, qPrintable(QStringLiteral("fps=%1").arg(fps, 0, 'f', 1)));
}

void TestMessageUi::ctrlKSearchFindsChatAndMessage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());
    seedChat(worker, QStringLiteral("chat-1"), QStringLiteral("alice"));
    seedMessage(worker, QStringLiteral("chat-1"), QStringLiteral("m1"),
                QStringLiteral("Привет из поиска"), 100);

    SearchService search(&worker);
    ChatList list;
    list.setChats({makeChat(QStringLiteral("chat-1"), QStringLiteral("alice"), 0, false, false)});
    list.setSearchService(&search);
    list.show();
    list.openSearch();
    QVERIFY(list.searchPanel()->isVisible());

    list.searchField()->setText(QStringLiteral("привет"));
    QTRY_VERIFY_WITH_TIMEOUT(list.searchResults()->count() >= 1, 3000);

    QSignalSpy messageSelected(&list, &ChatList::messageSelected);
    bool foundMessage = false;
    for (int row = 0; row < list.searchResults()->count(); ++row) {
        QListWidgetItem *item = list.searchResults()->item(row);
        if (item->text().contains(QStringLiteral("Привет"))) {
            list.searchResults()->itemClicked(item);
            foundMessage = true;
            break;
        }
    }
    QVERIFY(foundMessage);
    QCOMPARE(messageSelected.count(), 1);
    QCOMPARE(messageSelected.first().at(0).toString(), QStringLiteral("chat-1"));
    QCOMPARE(messageSelected.first().at(1).toString(), QStringLiteral("m1"));
}

void TestMessageUi::chatSearchJumpsAndHighlightsMatches()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.show();
    QCoreApplication::processEvents();
    view.setMessages({makeMessage(QStringLiteral("m1"), QStringLiteral("hello needle"), false,
                                  MessageState::Received, 100),
                      makeMessage(QStringLiteral("m2"), QStringLiteral("nothing here"), true,
                                  MessageState::Sent, 200),
                      makeMessage(QStringLiteral("m3"), QStringLiteral("needle again"), false,
                                  MessageState::Received, 300)});
    view.openSearch();
    QVERIFY(view.searchBar()->isVisible());
    view.searchField()->setText(QStringLiteral("needle"));
    QCOMPARE(view.searchMatchCount(), 2);
    QCOMPARE(view.searchCurrentId(), QStringLiteral("m1"));
    const int row = view.messageModel()->indexOf(QStringLiteral("m3"));
    QVERIFY(view.messageModel()
                ->data(view.messageModel()->index(row, 0),
                       MessageListModel::SearchMatchRole)
                .toBool());
    view.searchNext();
    QCOMPARE(view.searchCurrentId(), QStringLiteral("m3"));
    view.searchPrevious();
    QCOMPARE(view.searchCurrentId(), QStringLiteral("m1"));
    view.closeSearch();
    QCOMPARE(view.searchMatchCount(), 0);
}

void TestMessageUi::jumpToMessageHighlightsTheRow()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.setMessages({makeMessage(QStringLiteral("m1"), QStringLiteral("a"), false,
                                  MessageState::Received, 100),
                      makeMessage(QStringLiteral("m2"), QStringLiteral("b"), false,
                                  MessageState::Received, 200)});
    view.jumpToMessage(QStringLiteral("m2"));
    const int row = view.messageModel()->indexOf(QStringLiteral("m2"));
    QVERIFY(view.messageModel()
                ->data(view.messageModel()->index(row, 0),
                       MessageListModel::SearchMatchRole)
                .toBool());
}

void TestMessageUi::multiSelectCollectsIdsAndEscapes()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.show();
    QCoreApplication::processEvents();
    view.setMessages({makeMessage(QStringLiteral("m1"), QStringLiteral("a"), true,
                                  MessageState::Sent, 100),
                      makeMessage(QStringLiteral("m2"), QStringLiteral("b"), true,
                                  MessageState::Sent, 200)});
    view.enterMultiSelect(QStringLiteral("m1"));
    QVERIFY(view.multiSelectActive());
    QVERIFY(view.multiToolbar()->isVisible());
    view.messageView()->selectionModel()->select(
        view.messageModel()->index(view.messageModel()->indexOf(QStringLiteral("m2")), 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(view.selectedClientMsgIds().size(), 2);
    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.multiSelectActive());
    QVERIFY(view.selectedClientMsgIds().isEmpty());
}

void TestMessageUi::pinnedBannerShowsAndHides()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    view.show();
    QCoreApplication::processEvents();
    Message pinned = makeMessage(QStringLiteral("m1"), QStringLiteral("pin me"), false,
                                 MessageState::Received, 100);
    view.setPinnedMessages({pinned});
    QCOMPARE(view.pinnedCount(), 1);
    QVERIFY(view.pinnedBanner()->isVisible());
    view.setPinnedMessages({});
    QCOMPARE(view.pinnedCount(), 0);
    QVERIFY(!view.pinnedBanner()->isVisible());
}

void TestMessageUi::formattedMessageExposesEditedAndForwardedRoles()
{
    ChatView view;
    view.setChat(makeChat(QStringLiteral("chat"), QStringLiteral("Alice"), 0, false, false));
    Message message = makeMessage(QStringLiteral("m1"),
                                  QStringLiteral("**bold** @alice"),
                                  false, MessageState::Received, 100);
    message.editedAt = 1234;
    message.forwardedFrom = QStringLiteral("bob");
    view.setMessages({message});
    const QModelIndex index = view.messageModel()->index(0, 0);
    QVERIFY(view.messageModel()->data(index, MessageListModel::EditedRole).toBool());
    QCOMPARE(view.messageModel()->data(index, MessageListModel::ForwardedRole).toString(),
             QStringLiteral("bob"));
}

QTEST_MAIN(TestMessageUi)
#include "test_message_ui.moc"
