#include "services/ContactService.h"
#include "storage/ContactsRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

class TestContactService : public QObject
{
    Q_OBJECT

private slots:
    void resolvesExactLoginAndCachesContact();
    void unknownLoginIsAnError();
    void invalidLoginNeverReachesTheDirectory();
    void missingDirectoryIsReported();
    void listsSavedContacts();
};

void TestContactService::resolvesExactLoginAndCachesContact()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ContactService service(&worker);
    QStringList asked;
    service.setLookup([&](const QString &login, std::function<void(Result<PeerProfile>)> done) {
        asked.append(login);
        PeerProfile profile;
        profile.login = login;
        profile.displayName = QStringLiteral("Alice");
        profile.userId = QStringLiteral("u-alice");
        profile.found = true;
        done(Result<PeerProfile>::ok(profile));
    });

    PeerProfile profile;
    bool done = false;
    service.resolve(QStringLiteral("alice"), [&](Result<PeerProfile> result) {
        profile = result.isOk() ? result.value() : PeerProfile{};
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.first(), QStringLiteral("alice"));
    QVERIFY(profile.found);
    QCOMPARE(profile.displayName, QStringLiteral("Alice"));

    auto saved = worker.submit([](storage::Database &db) {
        auto contact = storage::ContactsRepo(db).getByLogin(QStringLiteral("alice"));
        return contact.isOk() ? contact.value() : std::optional<Contact>();
    });
    saved.waitForFinished();
    QVERIFY(saved.result().has_value());
    QCOMPARE(saved.result()->displayName, QStringLiteral("Alice"));
}

void TestContactService::unknownLoginIsAnError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ContactService service(&worker);
    service.setLookup([](const QString &, std::function<void(Result<PeerProfile>)> done) {
        done(Result<PeerProfile>::error(QStringLiteral("user_not_found")));
    });

    QString error;
    bool done = false;
    service.resolve(QStringLiteral("nobody"), [&](Result<PeerProfile> result) {
        error = result.error();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(error, QStringLiteral("user_not_found"));
}

void TestContactService::invalidLoginNeverReachesTheDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ContactService service(&worker);
    bool asked = false;
    service.setLookup([&](const QString &, std::function<void(Result<PeerProfile>)> done) {
        asked = true;
        done(Result<PeerProfile>::error(QStringLiteral("unexpected")));
    });

    QString error;
    bool done = false;
    service.resolve(QStringLiteral("a"), [&](Result<PeerProfile> result) {
        error = result.error();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(error, QStringLiteral("login.invalid_format"));
    QVERIFY(!asked);
}

void TestContactService::missingDirectoryIsReported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ContactService service(&worker);
    QVERIFY(!service.hasLookup());

    QString error;
    bool done = false;
    service.resolve(QStringLiteral("alice"), [&](Result<PeerProfile> result) {
        error = result.error();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(error, QStringLiteral("directory_unavailable"));
}

void TestContactService::listsSavedContacts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ContactService service(&worker);
    bool saved = false;
    Contact dave;
    dave.login = QStringLiteral("dave");
    dave.displayName = QStringLiteral("Dave");
    service.saveContact(dave, [&](Result<bool> result) {
        saved = result.isOk() && result.value();
    });
    QTRY_VERIFY_WITH_TIMEOUT(saved, 3000);

    QVector<Contact> contacts;
    bool done = false;
    service.listContacts([&](Result<QVector<Contact>> result) {
        contacts = result.isOk() ? result.value() : QVector<Contact>();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.first().login, QStringLiteral("dave"));
}

QTEST_MAIN(TestContactService)
#include "test_contact_service.moc"
