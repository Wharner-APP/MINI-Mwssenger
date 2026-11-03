#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>
#include <QVector>

class QSystemTrayIcon;

namespace mini {
class Settings;
}

namespace mini::services {

// One of the five built-in sounds (§46.3). The names map to WAV files in
// resources/sounds and to the resource paths returned by SoundPlayer.
enum class NotificationSound {
    Incoming, // msg_in.wav
    Outgoing, // msg_out.wav
    Mention,  // mention.wav
    Ring,     // ring.wav
    Error     // error.wav
};

// Everything the §46.2 rule table needs to know about one event. Defaults
// describe "window is open on another chat", which is the common case.
struct NotificationEvent
{
    QString chatId;
    QString title;
    QString body;
    bool mention = false;
    bool secret = false;        // secret chat: never show the body
    bool call = false;          // incoming call: ring + priority
    bool windowActive = true;   // the main window has focus
    bool chatOpen = false;      // the window is active AND this chat is shown
    bool windowMinimized = false;
    bool chatMuted = false;     // Chat::muted / Chat::mutedUntil from storage
};

// Result of applying the rule table. `counter` means "no notification, only
// the unread counter changes"; `accumulated` is the DND queue (§46.2).
struct NotificationDecision
{
    bool notify = false;
    bool toast = false;      // in-app toast: window active, another chat
    bool native = false;     // Windows tray balloon
    bool counter = false;    // chat list counter only
    bool accumulated = false;
    bool showText = false;   // false for secret chats and preview-off
    bool playSound = false;
    NotificationSound sound = NotificationSound::Incoming;
};

// Plays the five WAVs. The default implementation uses QSoundEffect; tests
// substitute a recording subclass without touching the audio device.
class SoundPlayer : public QObject
{
    Q_OBJECT

public:
    explicit SoundPlayer(QObject *parent = nullptr);
    ~SoundPlayer() override;

    // ":/sounds/msg_in.wav" style resource path for a sound.
    [[nodiscard]] static QString resourcePath(NotificationSound sound);
    // "msg_in.wav" style file name, also used by the generator and tests.
    [[nodiscard]] static QString fileName(NotificationSound sound);

    virtual void play(NotificationSound sound);
    [[nodiscard]] virtual bool isAvailable() const;

    void setVolume(qreal volume); // clamped to 0..1
    [[nodiscard]] qreal volume() const { return m_volume; }

private:
    QVector<QObject *> m_effects; // QSoundEffect*, created lazily
    qreal m_volume = 0.7;
};

// Decides and delivers notifications (§46): native Windows balloons through
// QSystemTrayIcon, in-app toasts, the five sounds and the DND accumulator.
// All rule evaluation is pure and synchronous, so the UI layer only has to
// build a NotificationEvent and connect the signals.
class NotificationService : public QObject
{
    Q_OBJECT

public:
    explicit NotificationService(Settings *settings, QObject *parent = nullptr);
    ~NotificationService() override;

    // Global switches (§46.4).
    [[nodiscard]] bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);
    [[nodiscard]] bool soundEnabled() const;
    void setSoundEnabled(bool enabled);
    [[nodiscard]] bool previewEnabled() const;
    void setPreviewEnabled(bool enabled);

    // Per-chat switches and temporary mute (§46.4 #171).
    [[nodiscard]] bool chatNotificationsEnabled(const QString &chatId) const;
    void setChatNotificationsEnabled(const QString &chatId, bool enabled);
    [[nodiscard]] bool chatSoundEnabled(const QString &chatId) const;
    void setChatSoundEnabled(const QString &chatId, bool enabled);
    // -1 means "forever", 0 unmutes, a positive value is an epoch-ms expiry.
    void setChatMutedUntil(const QString &chatId, qint64 untilMs);
    [[nodiscard]] qint64 chatMutedUntil(const QString &chatId) const;
    [[nodiscard]] bool chatMuted(const QString &chatId,
                                 const QDateTime &now = QDateTime::currentDateTime()) const;

    // Do not disturb (§23.3, §46.2). The manual flag shares the tray key
    // "ui/do_not_disturb" already written by MainWindow.
    [[nodiscard]] bool dndManual() const;
    void setDndManual(bool on);
    [[nodiscard]] bool dndScheduleEnabled() const;
    void setDndScheduleEnabled(bool on);
    [[nodiscard]] QTime dndStart() const;
    void setDndStart(const QTime &time);
    [[nodiscard]] QTime dndEnd() const;
    void setDndEnd(const QTime &time);
    [[nodiscard]] bool dndActive() const;
    [[nodiscard]] bool dndActiveAt(const QDateTime &now) const;
    // §46.2: 23:00-07:00 crosses midnight. Equal bounds mean "never".
    [[nodiscard]] static bool inQuietHours(const QTime &time, const QTime &start,
                                           const QTime &end);

    // DND stores what arrived while quiet (§46.2 "нет, копятся").
    [[nodiscard]] int accumulatedCount() const { return m_accumulated; }
    void clearAccumulated();

    // Pure rule evaluation; `now` is explicit so DND is testable.
    [[nodiscard]] NotificationDecision decide(
        const NotificationEvent &event,
        const QDateTime &now = QDateTime::currentDateTime()) const;

    // Applies decide() and delivers: signals, tray balloon, sound, counter.
    NotificationDecision notify(const NotificationEvent &event);

    // Explicit sounds for events outside the incoming pipeline: msg_out on
    // send, error on failure, ring from the call layer.
    void playSound(NotificationSound sound);

    void setSoundPlayer(SoundPlayer *player); // takes ownership
    [[nodiscard]] SoundPlayer *soundPlayer() const { return m_sounds; }

    void setTrayIcon(QSystemTrayIcon *tray) { m_tray = tray; }
    [[nodiscard]] QSystemTrayIcon *trayIcon() const { return m_tray; }

signals:
    // The chat list still has to raise its unread counter even when the
    // notification itself is suppressed (muted chat, global switch off).
    void counterRequested(const QString &chatId);
    void toastRequested(const QString &chatId, const QString &title, const QString &body);
    void nativeRequested(const QString &chatId, const QString &title, const QString &body);
    void soundPlayed(mini::services::NotificationSound sound);
    void accumulatedChanged(int count);
    void settingsChanged();

private:
    [[nodiscard]] QString chatKey(const QString &chatId, const QString &leaf) const;

    Settings *m_settings = nullptr;
    SoundPlayer *m_sounds = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    int m_accumulated = 0;
};

} // namespace mini::services

Q_DECLARE_METATYPE(mini::services::NotificationSound)
Q_DECLARE_METATYPE(mini::services::NotificationEvent)
Q_DECLARE_METATYPE(mini::services::NotificationDecision)
