#include "services/PrivacyService.h"

#include <QSignalSpy>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

class FakePrivacyApi : public PrivacyApi
{
public:
    net::PrivacySettings settings;
    QList<net::BlockedUserInfo> blocks;
    QList<net::DeviceRecord> devices;
    int revokeAllCalls = 0;

    void getSettings(SettingsDone done) override
    {
        done(net::ApiResult<net::PrivacySettings>::ok(settings));
    }

    void updateSettings(const net::PrivacySettings &value, SettingsDone done) override
    {
        settings = value;
        done(net::ApiResult<net::PrivacySettings>::ok(settings));
    }

    void listBlocks(BlocksDone done) override
    {
        done(net::ApiResult<QList<net::BlockedUserInfo>>::ok(blocks));
    }

    void blockUser(const QString &userOrLogin, EmptyDone done) override
    {
        net::BlockedUserInfo user;
        user.userId = QStringLiteral("u_") + userOrLogin;
        user.login = userOrLogin;
        user.displayName = userOrLogin;
        blocks.append(user);
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void unblockUser(const QString &userId, EmptyDone done) override
    {
        for (int i = 0; i < blocks.size(); ++i) {
            if (blocks.at(i).userId == userId || blocks.at(i).login == userId) {
                blocks.removeAt(i);
                break;
            }
        }
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void listDevices(DevicesDone done) override
    {
        done(net::ApiResult<QList<net::DeviceRecord>>::ok(devices));
    }

    void revokeDevice(const QString &deviceId, EmptyDone done) override
    {
        for (int i = 0; i < devices.size(); ++i) {
            if (devices.at(i).deviceId == deviceId) {
                devices.removeAt(i);
                break;
            }
        }
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void revokeAllDevices(RevokeDone done) override
    {
        ++revokeAllCalls;
        int revoked = 0;
        for (int i = devices.size() - 1; i >= 0; --i) {
            if (!devices.at(i).current) {
                devices.removeAt(i);
                ++revoked;
            }
        }
        net::RevokeAllResult result;
        result.revokedDevices = revoked;
        result.revokedSessions = revoked;
        done(net::ApiResult<net::RevokeAllResult>::ok(result));
    }
};

} // namespace

class TestPrivacyService : public QObject
{
    Q_OBJECT

private slots:
    void loadsAndSavesSettings();
    void blocksAndUnblocks();
    void listsAndRevokesDevices();
    void rejectsEmptyIdentifiers();
};

void TestPrivacyService::loadsAndSavesSettings()
{
    FakePrivacyApi api;
    api.settings.whoCanMessage = QStringLiteral("contacts");
    api.settings.whoCanCall = QStringLiteral("nobody");
    api.settings.whoSeesLastSeen = QStringLiteral("contacts");
    api.settings.readReceipts = false;
    PrivacyService service;
    service.setApi(&api);

    PrivacySettings loaded;
    service.loadSettings([&](Result<PrivacySettings> result) {
        if (result.isOk()) {
            loaded = result.value();
        }
    });
    QCOMPARE(loaded.whoCanMessage, PrivacyRule::Contacts);
    QCOMPARE(loaded.whoCanCall, PrivacyRule::Nobody);
    QCOMPARE(loaded.whoSeesLastSeen, PrivacyRule::Contacts);
    QVERIFY(!loaded.readReceipts);

    PrivacySettings changed;
    changed.whoCanMessage = PrivacyRule::Nobody;
    changed.whoCanCall = PrivacyRule::Contacts;
    changed.whoSeesLastSeen = PrivacyRule::Everyone;
    changed.readReceipts = true;
    QSignalSpy saved(&service, &PrivacyService::settingsChanged);
    bool ok = false;
    service.saveSettings(changed, [&](Result<PrivacySettings> result) {
        ok = result.isOk();
    });
    QVERIFY(ok);
    QCOMPARE(saved.count(), 1);
    QCOMPARE(api.settings.whoCanMessage, QStringLiteral("nobody"));
    QCOMPARE(api.settings.whoCanCall, QStringLiteral("contacts"));
    QCOMPARE(api.settings.whoSeesLastSeen, QStringLiteral("everyone"));
    QVERIFY(api.settings.readReceipts);
}

void TestPrivacyService::blocksAndUnblocks()
{
    FakePrivacyApi api;
    PrivacyService service;
    service.setApi(&api);

    QSignalSpy changed(&service, &PrivacyService::blocksChanged);
    bool blocked = false;
    service.block(QStringLiteral("@spammer"),
                  [&](Result<bool> result) { blocked = result.isOk(); });
    QVERIFY(blocked);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(api.blocks.size(), 1);
    QCOMPARE(api.blocks.first().login, QStringLiteral("spammer"));

    QVector<BlockedUser> listed;
    service.listBlocks([&](Result<QVector<BlockedUser>> result) {
        if (result.isOk()) {
            listed = result.value();
        }
    });
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().login, QStringLiteral("spammer"));

    bool unblocked = false;
    service.unblock(listed.first().userId,
                    [&](Result<bool> result) { unblocked = result.isOk(); });
    QVERIFY(unblocked);
    QVERIFY(api.blocks.isEmpty());
    QCOMPARE(changed.count(), 2);
}

void TestPrivacyService::listsAndRevokesDevices()
{
    FakePrivacyApi api;
    net::DeviceRecord current;
    current.deviceId = QStringLiteral("d_me");
    current.name = QStringLiteral("PC-Matvey");
    current.platform = QStringLiteral("windows");
    current.current = true;
    api.devices.append(current);
    net::DeviceRecord other;
    other.deviceId = QStringLiteral("d_other");
    other.name = QStringLiteral("Laptop");
    other.platform = QStringLiteral("windows");
    api.devices.append(other);

    PrivacyService service;
    service.setApi(&api);

    QVector<DeviceEntry> listed;
    service.listDevices([&](Result<QVector<DeviceEntry>> result) {
        if (result.isOk()) {
            listed = result.value();
        }
    });
    QCOMPARE(listed.size(), 2);
    QVERIFY(listed.at(0).current);
    QVERIFY(!listed.at(1).current);

    QSignalSpy changed(&service, &PrivacyService::devicesChanged);
    bool revoked = false;
    service.revokeDevice(QStringLiteral("d_other"),
                         [&](Result<bool> result) { revoked = result.isOk(); });
    QVERIFY(revoked);
    QCOMPARE(api.devices.size(), 1);
    QCOMPARE(changed.count(), 1);

    api.devices.append(other);
    int count = -1;
    bool ok = false;
    service.revokeAllOtherSessions([&](Result<int> result) {
        ok = result.isOk();
        if (ok) {
            count = result.value();
        }
    });
    QVERIFY(ok);
    QCOMPARE(count, 1);
    QCOMPARE(api.revokeAllCalls, 1);
    QCOMPARE(api.devices.size(), 1);
    QVERIFY(api.devices.first().current);
}

void TestPrivacyService::rejectsEmptyIdentifiers()
{
    FakePrivacyApi api;
    PrivacyService service;
    service.setApi(&api);

    bool blockOk = true;
    service.block(QStringLiteral("   "),
                  [&](Result<bool> result) { blockOk = result.isOk(); });
    QVERIFY(!blockOk);

    bool unblockOk = true;
    service.unblock(QString(),
                    [&](Result<bool> result) { unblockOk = result.isOk(); });
    QVERIFY(!unblockOk);

    bool deviceOk = true;
    service.revokeDevice(QString(),
                         [&](Result<bool> result) { deviceOk = result.isOk(); });
    QVERIFY(!deviceOk);
    QVERIFY(api.blocks.isEmpty());
}

QTEST_MAIN(TestPrivacyService)
#include "test_privacy_service.moc"
