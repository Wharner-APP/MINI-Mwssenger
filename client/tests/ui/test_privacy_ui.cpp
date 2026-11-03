#include "core/BuildInfo.h"
#include "core/Settings.h"
#include "services/PrivacyService.h"
#include "ui/BlockListDialog.h"
#include "ui/DevicesDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    int updates = 0;

    void getSettings(SettingsDone done) override
    {
        done(net::ApiResult<net::PrivacySettings>::ok(settings));
    }

    void updateSettings(const net::PrivacySettings &value, SettingsDone done) override
    {
        ++updates;
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
            if (blocks.at(i).userId == userId) {
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
        net::RevokeAllResult result;
        for (int i = devices.size() - 1; i >= 0; --i) {
            if (!devices.at(i).current) {
                devices.removeAt(i);
                ++result.revokedDevices;
            }
        }
        done(net::ApiResult<net::RevokeAllResult>::ok(result));
    }
};

} // namespace

class TestPrivacyUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }

    void settingsDialogSavesPrivacyRules();
    void blockListDialogBlocksAndUnblocks();
    void devicesDialogMarksCurrentAndRevokesAnother();
};

void TestPrivacyUi::settingsDialogSavesPrivacyRules()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakePrivacyApi api;
    api.settings.whoCanMessage = QStringLiteral("contacts");
    api.settings.whoCanCall = QStringLiteral("nobody");
    api.settings.whoSeesLastSeen = QStringLiteral("everyone");
    api.settings.readReceipts = true;
    PrivacyService service;
    service.setApi(&api);

    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&settings);
    dialog.setPrivacyService(&service);

    QCOMPARE(dialog.privacyMessageCombo()->currentData().toString(),
             QStringLiteral("contacts"));
    QCOMPARE(dialog.privacyCallCombo()->currentData().toString(),
             QStringLiteral("nobody"));
    QVERIFY(dialog.privacyReceiptsCheckBox()->isChecked());
    QVERIFY(dialog.blockedUsersButton()->isEnabled());
    QVERIFY(dialog.devicesButton()->isEnabled());

    const int nobody = dialog.privacyMessageCombo()->findData(QStringLiteral("nobody"));
    QVERIFY(nobody >= 0);
    dialog.privacyMessageCombo()->setCurrentIndex(nobody);
    QTRY_COMPARE_WITH_TIMEOUT(api.settings.whoCanMessage, QStringLiteral("nobody"), 3000);
    QVERIFY(api.updates >= 1);

    dialog.privacyReceiptsCheckBox()->setChecked(false);
    QTRY_VERIFY_WITH_TIMEOUT(!api.settings.readReceipts, 3000);

    // A null service disables the controls instead of faking a save.
    dialog.setPrivacyService(nullptr);
    QVERIFY(!dialog.privacyMessageCombo()->isEnabled());
    QVERIFY(!dialog.devicesButton()->isEnabled());
}

void TestPrivacyUi::blockListDialogBlocksAndUnblocks()
{
    FakePrivacyApi api;
    net::BlockedUserInfo blocked;
    blocked.userId = QStringLiteral("u_spammer");
    blocked.login = QStringLiteral("spammer");
    blocked.displayName = QStringLiteral("Spammer");
    api.blocks.append(blocked);

    PrivacyService service;
    service.setApi(&api);

    BlockListDialog dialog(&service);
    QCOMPARE(dialog.blockList()->count(), 1);
    QCOMPARE(dialog.blocked().size(), 1);

    dialog.loginEdit()->setText(QStringLiteral("troll"));
    dialog.blockButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(dialog.blockList()->count(), 2, 3000);
    QCOMPARE(api.blocks.size(), 2);
    QVERIFY(dialog.loginEdit()->text().isEmpty());

    dialog.blockList()->setCurrentRow(0);
    QVERIFY(dialog.unblockButton()->isEnabled());
    dialog.unblockButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(dialog.blockList()->count(), 1, 3000);
    QCOMPARE(api.blocks.size(), 1);
}

void TestPrivacyUi::devicesDialogMarksCurrentAndRevokesAnother()
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

    DevicesDialog dialog(&service);
    QCOMPARE(dialog.deviceList()->count(), 2);
    QVERIFY(dialog.revokeAllButton()->isEnabled());
    QVERIFY(!dialog.revokeButton()->isEnabled());

    dialog.deviceList()->setCurrentRow(0);
    QVERIFY(!dialog.revokeButton()->isEnabled()); // current device

    dialog.deviceList()->setCurrentRow(1);
    QVERIFY(dialog.revokeButton()->isEnabled());
    dialog.revokeButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(dialog.deviceList()->count(), 1, 3000);
    QCOMPARE(api.devices.size(), 1);
    QVERIFY(api.devices.first().current);
    QVERIFY(!dialog.revokeAllButton()->isEnabled());
}

QTEST_MAIN(TestPrivacyUi)
#include "test_privacy_ui.moc"
