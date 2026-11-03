#include "domain/ChatFolder.h"
#include "services/FolderService.h"
#include "storage/FoldersRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

template <typename T>
T waitFor(std::function<void(std::function<void(Result<T>)>)> start)
{
    bool done = false;
    T value{};
    start([&](Result<T> result) {
        done = true;
        if (result.isOk()) {
            value = result.value();
        }
    });
    for (int i = 0; i < 400 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return value;
}

QString waitError(std::function<void(std::function<void(Result<ChatFolder>)>)> start)
{
    bool done = false;
    QString error;
    start([&](Result<ChatFolder> result) {
        done = true;
        if (result.isError()) {
            error = result.error();
        }
    });
    for (int i = 0; i < 400 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return error;
}

ChatFolder folder(const QString &title)
{
    ChatFolder value;
    value.title = title;
    return value;
}

} // namespace

class TestFolders : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rulesMatchTypeNameMuteAndUnread();
    void excludesWin();
    void limitIsTen();
    void reorderAndRemovePersist();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::StorageWorker> m_worker;
    std::unique_ptr<FolderService> m_service;
    int m_counter = 0;
};

void TestFolders::init()
{
    QVERIFY(m_dir.isValid());
    m_worker = std::make_unique<storage::StorageWorker>();
    QVERIFY(m_worker
                ->open(m_dir.filePath(QStringLiteral("folders-%1.db").arg(++m_counter)),
                       dbKey())
                .isOk());
    m_service = std::make_unique<FolderService>(m_worker.get());
}

void TestFolders::cleanup()
{
    m_service.reset();
    m_worker.reset();
}

void TestFolders::rulesMatchTypeNameMuteAndUnread()
{
    Chat direct;
    direct.chatId = QStringLiteral("direct_1");
    direct.kind = QStringLiteral("direct");
    direct.title = QStringLiteral("Alice");
    direct.peerLogin = QStringLiteral("alice");
    direct.unreadCount = 3;

    Chat group;
    group.chatId = QStringLiteral("group_1");
    group.kind = QStringLiteral("group");
    group.title = QStringLiteral("Work chat");
    group.muted = true;
    group.unreadCount = 0;

    ChatFolder work;
    work.title = QStringLiteral("Work");
    work.includeTypes = {QStringLiteral("group"), QStringLiteral("channel")};
    QVERIFY(folderMatches(work, group));
    QVERIFY(!folderMatches(work, direct));

    ChatFolder byName;
    byName.title = QStringLiteral("Alice");
    byName.nameContains = QStringLiteral("ali");
    QVERIFY(folderMatches(byName, direct));
    QVERIFY(!folderMatches(byName, group));

    ChatFolder unread;
    unread.title = QStringLiteral("Unread");
    unread.onlyUnread = true;
    QVERIFY(folderMatches(unread, direct));
    QVERIFY(!folderMatches(unread, group));

    ChatFolder muted;
    muted.title = QStringLiteral("Muted");
    muted.onlyMuted = true;
    QVERIFY(folderMatches(muted, group));
    QVERIFY(!folderMatches(muted, direct));

    ChatFolder explicitChats;
    explicitChats.title = QStringLiteral("Custom");
    explicitChats.includeChats = {QStringLiteral("group_1")};
    QVERIFY(folderMatches(explicitChats, group));
    QVERIFY(!folderMatches(explicitChats, direct));
}

void TestFolders::excludesWin()
{
    Chat group;
    group.chatId = QStringLiteral("group_1");
    group.kind = QStringLiteral("group");
    group.title = QStringLiteral("Work");

    ChatFolder folderRule;
    folderRule.title = QStringLiteral("Groups");
    folderRule.includeTypes = {QStringLiteral("group")};
    folderRule.excludeChats = {QStringLiteral("group_1")};
    QVERIFY(!folderMatches(folderRule, group));

    folderRule.excludeChats.clear();
    folderRule.excludeTypes = {QStringLiteral("group")};
    QVERIFY(!folderMatches(folderRule, group));

    folderRule.excludeTypes.clear();
    QVERIFY(folderMatches(folderRule, group));
}

void TestFolders::limitIsTen()
{
    for (int i = 0; i < kMaxFolders; ++i) {
        const ChatFolder saved = waitFor<ChatFolder>([this, i](auto done) {
            m_service->saveFolder(folder(QStringLiteral("F%1").arg(i)), done);
        });
        QVERIFY(!saved.id.isEmpty());
    }
    QCOMPARE(waitError([this](auto done) {
                 m_service->saveFolder(folder(QStringLiteral("Eleven")), done);
             }),
             QStringLiteral("folder.limit"));

    // Updating an existing folder is still allowed at the limit.
    auto listed = m_worker->submit([](storage::Database &db) {
        return storage::FoldersRepo(db).list().value();
    });
    listed.waitForFinished();
    QCOMPARE(listed.result().size(), kMaxFolders);
    ChatFolder renamed = listed.result().first();
    renamed.title = QStringLiteral("Renamed");
    const ChatFolder updated = waitFor<ChatFolder>([this, renamed](auto done) {
        m_service->saveFolder(renamed, done);
    });
    QCOMPARE(updated.title, QStringLiteral("Renamed"));
}

void TestFolders::reorderAndRemovePersist()
{
    QStringList ids;
    for (const QString &title : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        const ChatFolder saved = waitFor<ChatFolder>([this, title](auto done) {
            m_service->saveFolder(folder(title), done);
        });
        ids.append(saved.id);
    }
    const QStringList reordered{ids.at(2), ids.at(0), ids.at(1)};
    bool done = false;
    m_service->reorder(reordered, [&done](Result<bool>) { done = true; });
    for (int i = 0; i < 200 && !done; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    QVERIFY(done);

    auto listed = m_worker->submit([](storage::Database &db) {
        return storage::FoldersRepo(db).list().value();
    });
    listed.waitForFinished();
    QStringList stored;
    for (const ChatFolder &value : listed.result()) {
        stored.append(value.id);
    }
    QCOMPARE(stored, reordered);

    bool removed = false;
    m_service->removeFolder(ids.at(1), [&removed](Result<bool> result) {
        removed = result.isOk() && result.value();
    });
    for (int i = 0; i < 200 && !removed; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    QVERIFY(removed);

    listed = m_worker->submit([](storage::Database &db) {
        return storage::FoldersRepo(db).list().value();
    });
    listed.waitForFinished();
    QCOMPARE(listed.result().size(), 2);
}

QTEST_MAIN(TestFolders)
#include "test_folders.moc"
