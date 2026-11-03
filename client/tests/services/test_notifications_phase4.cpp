// Phase 4 acceptance tests for notifications, sounds and Do not disturb.
// Not registered in CMakeLists.txt on purpose: the build owner adds this
// file to the services test list separately.
//
//   test_notifications_phase4.cpp

#include "core/Settings.h"
#include "services/NotificationService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

class FakeSoundPlayer : public SoundPlayer
{
public:
    QVector<NotificationSound> played;

    void play(NotificationSound sound) override { played.append(sound); }
    [[nodiscard]] bool isAvailable() const override { return true; }
};

NotificationEvent eventFor(const QString &chatId = QStringLiteral("direct:alice:bob"))
{
    NotificationEvent event;
    event.chatId = chatId;
    event.title = QStringLiteral("Alice");
    event.body = QStringLiteral("Hello there");
    event.windowActive = true;
    event.chatOpen = false;
    return event;
}

QDateTime at(int hour, int minute)
{
    return QDateTime(QDate(2026, 11, 4), QTime(hour, minute));
}

} // namespace

class TestNotificationsPhase4 : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §46.2 window rules
    void activeWindowOpenChatSuppresses();
    void activeWindowOtherChatShowsToast();
    void minimizedOrInactiveWindowShowsNative();

    // §46.2 mute
    void mutedChatOnlyBumpsCounter();
    void chatMuteExpiry();

    // §46.2 mentions and secret chats
    void mentionAlwaysSurfaces();
    void mentionStillUsesMentionSoundWhenChatSoundOff();
    void secretChatHidesText();
    void previewSwitchHidesText();

    // §46.2 / §46.4 Do not disturb
    void dndManualAccumulates();
    void dndCallSurfacesWithoutRing();
    void dndScheduleWindow();
    void quietHoursWrapAround();
    void accumulatedCountClears();

    // §46.4 per-chat switches
    void perChatSwitches();
    void globalSwitchWins();

    // §46.3 sounds
    void soundFilesAreEmbedded();
    void soundDelivery();
    void soundSwitchSuppressesPlayback();

    // delivery signals
    void toastAndNativeSignals();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<NotificationService> m_service;
};

void TestNotificationsPhase4::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(
        QDir(m_dir->path()).filePath(QStringLiteral("settings.ini")));
    m_service = std::make_unique<NotificationService>(m_settings.get());
    // Keep the audio backend out of the headless test run.
    m_service->setSoundPlayer(new FakeSoundPlayer);
}

void TestNotificationsPhase4::cleanup()
{
    m_service.reset();
    m_settings.reset();
    m_dir.reset();
}

void TestNotificationsPhase4::activeWindowOpenChatSuppresses()
{
    auto event = eventFor();
    event.chatOpen = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(!decision.notify);
    QVERIFY(!decision.toast);
    QVERIFY(!decision.native);
    QVERIFY(!decision.playSound);
}

void TestNotificationsPhase4::activeWindowOtherChatShowsToast()
{
    const auto decision = m_service->decide(eventFor(), at(12, 0));

    QVERIFY(decision.notify);
    QVERIFY(decision.toast);
    QVERIFY(!decision.native);
    QVERIFY(decision.showText);
    QVERIFY(decision.playSound);
    QCOMPARE(decision.sound, NotificationSound::Incoming);
}

void TestNotificationsPhase4::minimizedOrInactiveWindowShowsNative()
{
    auto minimized = eventFor();
    minimized.windowMinimized = true;
    auto decision = m_service->decide(minimized, at(12, 0));
    QVERIFY(decision.notify);
    QVERIFY(decision.native);
    QVERIFY(!decision.toast);

    auto inactive = eventFor();
    inactive.windowActive = false;
    inactive.windowMinimized = false;
    decision = m_service->decide(inactive, at(12, 0));
    QVERIFY(decision.notify);
    QVERIFY(decision.native);
}

void TestNotificationsPhase4::mutedChatOnlyBumpsCounter()
{
    auto event = eventFor();
    event.chatMuted = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(!decision.notify);
    QVERIFY(decision.counter);
    QVERIFY(!decision.playSound);

    QSignalSpy counterSpy(m_service.get(), &NotificationService::counterRequested);
    m_service->notify(event);
    QCOMPARE(counterSpy.count(), 1);
    QCOMPARE(counterSpy.first().at(0).toString(), event.chatId);
}

void TestNotificationsPhase4::chatMuteExpiry()
{
    const QString chatId = QStringLiteral("direct:alice:bob");

    QVERIFY(!m_service->chatMuted(chatId, at(12, 0)));

    const QDateTime now = at(12, 0);
    m_service->setChatMutedUntil(chatId, now.addSecs(3600).toMSecsSinceEpoch());
    QVERIFY(m_service->chatMuted(chatId, now));
    QVERIFY(!m_service->chatMuted(chatId, now.addSecs(7200)));

    // -1 is "forever"; 0 unmutes.
    m_service->setChatMutedUntil(chatId, -1);
    QVERIFY(m_service->chatMuted(chatId, now));
    QVERIFY(m_service->chatMuted(chatId, now.addYears(10)));
    m_service->setChatMutedUntil(chatId, 0);
    QVERIFY(!m_service->chatMuted(chatId, now));

    // The persisted mute feeds decide(), not only the getter.
    m_service->setChatMutedUntil(chatId, -1);
    const auto decision = m_service->decide(eventFor(chatId), now);
    QVERIFY(decision.counter);
    QVERIFY(!decision.notify);
}

void TestNotificationsPhase4::mentionAlwaysSurfaces()
{
    auto event = eventFor();
    event.chatMuted = true;
    event.mention = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(decision.notify);
    QVERIFY(decision.toast);
    QVERIFY(decision.playSound);
    QCOMPARE(decision.sound, NotificationSound::Mention);
}

void TestNotificationsPhase4::mentionStillUsesMentionSoundWhenChatSoundOff()
{
    const QString chatId = QStringLiteral("direct:alice:bob");
    m_service->setChatSoundEnabled(chatId, false);

    auto event = eventFor(chatId);
    event.mention = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(decision.playSound);
    QCOMPARE(decision.sound, NotificationSound::Mention);

    // A plain message in the same chat is silent but still visible.
    const auto plain = m_service->decide(eventFor(chatId), at(12, 0));
    QVERIFY(plain.notify);
    QVERIFY(!plain.playSound);
}

void TestNotificationsPhase4::secretChatHidesText()
{
    auto event = eventFor();
    event.secret = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(decision.notify);
    QVERIFY(!decision.showText);
}

void TestNotificationsPhase4::previewSwitchHidesText()
{
    m_service->setPreviewEnabled(false);
    const auto decision = m_service->decide(eventFor(), at(12, 0));

    QVERIFY(decision.notify);
    QVERIFY(!decision.showText);
}

void TestNotificationsPhase4::dndManualAccumulates()
{
    m_service->setDndManual(true);
    QVERIFY(m_service->dndActive());

    const auto decision = m_service->decide(eventFor(), at(12, 0));
    QVERIFY(decision.accumulated);
    QVERIFY(!decision.notify);
    QVERIFY(!decision.playSound);

    // Mentions are queued too, they do not break through DND.
    auto mention = eventFor();
    mention.mention = true;
    QVERIFY(m_service->decide(mention, at(12, 0)).accumulated);
}

void TestNotificationsPhase4::dndCallSurfacesWithoutRing()
{
    m_service->setDndManual(true);
    auto event = eventFor();
    event.call = true;
    const auto decision = m_service->decide(event, at(12, 0));

    QVERIFY(decision.notify);
    QVERIFY(decision.native);
    QVERIFY(!decision.playSound);
    QVERIFY(!decision.accumulated);
}

void TestNotificationsPhase4::dndScheduleWindow()
{
    // Off by default, even inside 23:00-07:00.
    QVERIFY(!m_service->dndScheduleEnabled());
    QVERIFY(!m_service->dndActiveAt(at(23, 30)));
    QCOMPARE(m_service->dndStart(), QTime(23, 0));
    QCOMPARE(m_service->dndEnd(), QTime(7, 0));

    m_service->setDndScheduleEnabled(true);
    QVERIFY(m_service->dndActiveAt(at(23, 30)));
    QVERIFY(m_service->dndActiveAt(at(23, 0)));
    QVERIFY(m_service->dndActiveAt(at(2, 0)));
    QVERIFY(m_service->dndActiveAt(at(6, 59)));
    QVERIFY(!m_service->dndActiveAt(at(7, 0)));
    QVERIFY(!m_service->dndActiveAt(at(12, 0)));
    QVERIFY(!m_service->dndActiveAt(at(22, 59)));
}

void TestNotificationsPhase4::quietHoursWrapAround()
{
    QVERIFY(NotificationService::inQuietHours(QTime(23, 30), QTime(23, 0), QTime(7, 0)));
    QVERIFY(NotificationService::inQuietHours(QTime(0, 1), QTime(23, 0), QTime(7, 0)));
    QVERIFY(!NotificationService::inQuietHours(QTime(7, 0), QTime(23, 0), QTime(7, 0)));
    // A same-day window and equal bounds.
    QVERIFY(NotificationService::inQuietHours(QTime(13, 0), QTime(12, 0), QTime(14, 0)));
    QVERIFY(!NotificationService::inQuietHours(QTime(11, 59), QTime(12, 0), QTime(14, 0)));
    QVERIFY(!NotificationService::inQuietHours(QTime(12, 0), QTime(12, 0), QTime(12, 0)));
}

void TestNotificationsPhase4::accumulatedCountClears()
{
    m_service->setDndManual(true);
    QSignalSpy spy(m_service.get(), &NotificationService::accumulatedChanged);

    m_service->notify(eventFor());
    m_service->notify(eventFor());
    QCOMPARE(m_service->accumulatedCount(), 2);
    QCOMPARE(spy.count(), 2);

    m_service->clearAccumulated();
    QCOMPARE(m_service->accumulatedCount(), 0);
    QCOMPARE(spy.count(), 3);
}

void TestNotificationsPhase4::perChatSwitches()
{
    const QString chatId = QStringLiteral("direct:alice:bob");
    QVERIFY(m_service->chatNotificationsEnabled(chatId));
    QVERIFY(m_service->chatSoundEnabled(chatId));

    m_service->setChatNotificationsEnabled(chatId, false);
    auto decision = m_service->decide(eventFor(chatId), at(12, 0));
    QVERIFY(decision.counter);
    QVERIFY(!decision.notify);
    QVERIFY(!decision.playSound);

    // Switches survive a restart because they live in Settings.
    auto restored = std::make_unique<NotificationService>(m_settings.get());
    QVERIFY(!restored->chatNotificationsEnabled(chatId));
}

void TestNotificationsPhase4::globalSwitchWins()
{
    m_service->setNotificationsEnabled(false);
    auto decision = m_service->decide(eventFor(), at(12, 0));
    QVERIFY(!decision.notify);
    QVERIFY(!decision.playSound);
    QVERIFY(!decision.accumulated);
}

void TestNotificationsPhase4::soundFilesAreEmbedded()
{
    QCOMPARE(SoundPlayer::fileName(NotificationSound::Incoming),
             QStringLiteral("msg_in.wav"));
    QCOMPARE(SoundPlayer::fileName(NotificationSound::Outgoing),
             QStringLiteral("msg_out.wav"));
    QCOMPARE(SoundPlayer::fileName(NotificationSound::Mention),
             QStringLiteral("mention.wav"));
    QCOMPARE(SoundPlayer::fileName(NotificationSound::Ring),
             QStringLiteral("ring.wav"));
    QCOMPARE(SoundPlayer::fileName(NotificationSound::Error),
             QStringLiteral("error.wav"));

    const QVector<NotificationSound> all{
        NotificationSound::Incoming, NotificationSound::Outgoing,
        NotificationSound::Mention, NotificationSound::Ring,
        NotificationSound::Error};
    for (NotificationSound sound : all) {
        const QString path = SoundPlayer::resourcePath(sound);
        QVERIFY2(QFile::exists(path),
                 qPrintable(QStringLiteral("missing resource ") + path));
    }
}

void TestNotificationsPhase4::soundDelivery()
{
    auto *fake = new FakeSoundPlayer;
    m_service->setSoundPlayer(fake);

    m_service->notify(eventFor());
    QCOMPARE(fake->played.size(), 1);
    QCOMPARE(fake->played.at(0), NotificationSound::Incoming);

    auto mention = eventFor();
    mention.mention = true;
    m_service->notify(mention);
    QCOMPARE(fake->played.size(), 2);
    QCOMPARE(fake->played.at(1), NotificationSound::Mention);

    // Explicit outgoing and error cues are public API.
    m_service->playSound(NotificationSound::Outgoing);
    m_service->playSound(NotificationSound::Error);
    QCOMPARE(fake->played.size(), 4);
    QCOMPARE(fake->played.at(2), NotificationSound::Outgoing);
    QCOMPARE(fake->played.at(3), NotificationSound::Error);
}

void TestNotificationsPhase4::soundSwitchSuppressesPlayback()
{
    auto *fake = new FakeSoundPlayer;
    m_service->setSoundPlayer(fake);

    m_service->setSoundEnabled(false);
    m_service->notify(eventFor());
    m_service->playSound(NotificationSound::Ring);
    QVERIFY(fake->played.isEmpty());

    m_service->setSoundEnabled(true);
    m_service->setDndManual(true);
    m_service->notify(eventFor());
    QVERIFY(fake->played.isEmpty());
}

void TestNotificationsPhase4::toastAndNativeSignals()
{
    QSignalSpy toastSpy(m_service.get(), &NotificationService::toastRequested);
    QSignalSpy nativeSpy(m_service.get(), &NotificationService::nativeRequested);

    m_service->notify(eventFor());
    QCOMPARE(toastSpy.count(), 1);
    QCOMPARE(nativeSpy.count(), 0);
    QCOMPARE(toastSpy.first().at(0).toString(), QStringLiteral("direct:alice:bob"));
    QCOMPARE(toastSpy.first().at(2).toString(), QStringLiteral("Hello there"));

    auto native = eventFor();
    native.windowMinimized = true;
    m_service->notify(native);
    QCOMPARE(toastSpy.count(), 1);
    QCOMPARE(nativeSpy.count(), 1);
}

QTEST_MAIN(TestNotificationsPhase4)
#include "test_notifications_phase4.moc"
