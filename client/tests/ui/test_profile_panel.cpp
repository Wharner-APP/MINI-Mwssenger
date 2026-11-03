#include "core/BuildInfo.h"
#include "core/Clock.h"
#include "core/Settings.h"
#include "services/ContactService.h"
#include "ui/ProfilePanel.h"
#include "ui/Theme.h"
#include "ui/widgets/Avatar.h"

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

SelfProfile sampleProfile()
{
    SelfProfile profile;
    profile.userId = QStringLiteral("u-1");
    profile.login = QStringLiteral("matvey");
    profile.displayName = QStringLiteral("Матвей Иванович");
    profile.bio = QStringLiteral("Учусь и играю в CS");
    profile.online = false;
    profile.lastActiveAt = 1000;
    return profile;
}

class ServiceStub
{
public:
    bool called = false;
    QString lastName;
    QString lastBio;

    void attach(ContactService &service)
    {
        service.setSelfSource(
            [](std::function<void(Result<SelfProfile>)> done) {
                done(Result<SelfProfile>::ok(sampleProfile()));
            },
            [this](const QString &displayName, const QString &bio,
                   std::function<void(Result<SelfProfile>)> done) {
                called = true;
                lastName = displayName;
                lastBio = bio;
                SelfProfile profile = sampleProfile();
                profile.displayName = displayName;
                profile.bio = bio;
                done(Result<SelfProfile>::ok(profile));
            });
    }
};

} // namespace

class TestProfilePanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Resources::initialize();
        Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                                Density::Comfortable);
    }
    void initialsComeFromDisplayName();
    void showsLoginNameBioAndPresence();
    void savesNameAndBioAcrossRestart();
    void rejectsEmptyNameAndLongBio();
    void usesTheServiceWhenItCanUpdate();
};

void TestProfilePanel::initialsComeFromDisplayName()
{
    QCOMPARE(ContactService::initialsFor(QStringLiteral("Alice")), QStringLiteral("A"));
    QCOMPARE(ContactService::initialsFor(QStringLiteral("Alice Bob")), QStringLiteral("AB"));
    QCOMPARE(ContactService::initialsFor(QStringLiteral("Матвей Иванович")),
             QStringLiteral("МИ"));
    QCOMPARE(ContactService::initialsFor(QString(), QStringLiteral("fallback")),
             QStringLiteral("F"));
    QCOMPARE(ContactService::initialsFor(QString()), QStringLiteral("?"));
}

void TestProfilePanel::showsLoginNameBioAndPresence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));

    ProfilePanel panel(&settings);
    SelfProfile profile = sampleProfile();
    profile.lastActiveAt = Clock::nowMs() - 5 * 60 * 1000;
    panel.setProfile(profile);

    QCOMPARE(panel.displayNameEdit()->text(), profile.displayName);
    QCOMPARE(panel.loginLabel()->text(), QStringLiteral("matvey"));
    QCOMPARE(panel.bioEdit()->toPlainText(), profile.bio);
    QCOMPARE(panel.avatar()->initials(), QStringLiteral("МИ"));
    QVERIFY(panel.statusLabel()->text().contains(QStringLiteral("5m")));

    profile.online = true;
    panel.setProfile(profile);
    QCOMPARE(panel.statusLabel()->text(), QStringLiteral("Online"));
}

void TestProfilePanel::savesNameAndBioAcrossRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(path);
        ProfilePanel panel(&settings);
        panel.setProfile(sampleProfile());
        panel.displayNameEdit()->setText(QStringLiteral("Новое Имя"));
        panel.bioEdit()->setPlainText(QStringLiteral("Новое био"));
        QSignalSpy updated(&panel, &ProfilePanel::profileUpdated);
        panel.saveButton()->click();
        QCOMPARE(updated.count(), 1);
        QVERIFY(panel.errorLabel()->text().isEmpty());
    }

    Settings reopened(path);
    ProfilePanel fresh(&reopened);
    QCOMPARE(fresh.displayNameEdit()->text(), QStringLiteral("Новое Имя"));
    QCOMPARE(fresh.bioEdit()->toPlainText(), QStringLiteral("Новое био"));
    QCOMPARE(fresh.loginLabel()->text(), QStringLiteral("matvey"));
}

void TestProfilePanel::rejectsEmptyNameAndLongBio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    ProfilePanel panel(&settings);
    panel.setProfile(sampleProfile());

    QSignalSpy updated(&panel, &ProfilePanel::profileUpdated);
    panel.displayNameEdit()->clear();
    panel.saveButton()->click();
    QVERIFY(!panel.errorLabel()->text().isEmpty());
    QCOMPARE(updated.count(), 0);

    panel.displayNameEdit()->setText(QStringLiteral("Good"));
    panel.bioEdit()->setPlainText(QString(201, QLatin1Char('a')));
    panel.saveButton()->click();
    QVERIFY(!panel.errorLabel()->text().isEmpty());
    QCOMPARE(updated.count(), 0);
}

void TestProfilePanel::usesTheServiceWhenItCanUpdate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.ini")));
    ContactService service(nullptr);
    ServiceStub stub;
    stub.attach(service);
    QVERIFY(service.hasSelfSource());

    ProfilePanel panel(&settings);
    panel.setContactService(&service);
    panel.setProfile(sampleProfile());
    panel.displayNameEdit()->setText(QStringLiteral("Matvey"));
    panel.bioEdit()->setPlainText(QStringLiteral("Новое био"));

    QSignalSpy updated(&panel, &ProfilePanel::profileUpdated);
    panel.saveButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(updated.count(), 1, 3000);
    QVERIFY(stub.called);
    QCOMPARE(stub.lastName, QStringLiteral("Matvey"));
    QCOMPARE(panel.profile().displayName, QStringLiteral("Matvey"));
    QCOMPARE(panel.profile().bio, QStringLiteral("Новое био"));
}

QTEST_MAIN(TestProfilePanel)
#include "test_profile_panel.moc"
