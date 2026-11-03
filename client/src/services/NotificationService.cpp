#include "services/NotificationService.h"

#include "core/Settings.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QUrl>

#ifdef MINI_HAVE_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace mini::services {

namespace {

constexpr auto kEnabledKey = "notifications/enabled";
constexpr auto kSoundKey = "notifications/sound";
constexpr auto kPreviewKey = "notifications/preview";
constexpr auto kDndManualKey = "ui/do_not_disturb"; // shared with the tray menu
constexpr auto kDndScheduleKey = "notifications/dnd/schedule";
constexpr auto kDndStartKey = "notifications/dnd/start";
constexpr auto kDndEndKey = "notifications/dnd/end";
constexpr auto kChatGroup = "notifications/chat/";

QString timeText(const QTime &time)
{
    return time.isValid() ? time.toString(QStringLiteral("HH:mm"))
                          : QStringLiteral("23:00");
}

QTime parseTime(const QString &text, const QTime &fallback)
{
    const QTime parsed = QTime::fromString(text, QStringLiteral("HH:mm"));
    return parsed.isValid() ? parsed : fallback;
}

} // namespace

// ---------------------------------------------------------------- SoundPlayer

SoundPlayer::SoundPlayer(QObject *parent) : QObject(parent) {}

SoundPlayer::~SoundPlayer()
{
    // QSoundEffect objects are children of this object; nothing to do here.
    m_effects.clear();
}

QString SoundPlayer::fileName(NotificationSound sound)
{
    switch (sound) {
    case NotificationSound::Outgoing:
        return QStringLiteral("msg_out.wav");
    case NotificationSound::Mention:
        return QStringLiteral("mention.wav");
    case NotificationSound::Ring:
        return QStringLiteral("ring.wav");
    case NotificationSound::Error:
        return QStringLiteral("error.wav");
    case NotificationSound::Incoming:
    default:
        return QStringLiteral("msg_in.wav");
    }
}

QString SoundPlayer::resourcePath(NotificationSound sound)
{
    return QStringLiteral(":/sounds/") + fileName(sound);
}

bool SoundPlayer::isAvailable() const
{
#ifdef MINI_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

void SoundPlayer::setVolume(qreal volume)
{
    m_volume = qBound(0.0, volume, 1.0);
}

void SoundPlayer::play(NotificationSound sound)
{
#ifdef MINI_HAVE_MULTIMEDIA
    const int index = static_cast<int>(sound);
    if (index < 0 || index > static_cast<int>(NotificationSound::Error)) {
        return;
    }
    while (m_effects.size() <= index) {
        m_effects.append(nullptr);
    }
    auto *effect = static_cast<QSoundEffect *>(m_effects.at(index));
    if (!effect) {
        effect = new QSoundEffect(this);
        effect->setSource(QUrl(QStringLiteral("qrc") + resourcePath(sound)));
        effect->setLoopCount(1);
        m_effects[index] = effect;
    }
    if (effect->status() == QSoundEffect::Error) {
        return;
    }
    effect->setVolume(m_volume);
    // Restart even when the same effect is still playing (rapid messages).
    effect->stop();
    effect->play();
#else
    Q_UNUSED(sound);
    // Qt Multimedia is not part of this build; keep a audible cue without
    // pulling in another dependency.
    QApplication::beep();
#endif
}

// ---------------------------------------------------------- NotificationService

NotificationService::NotificationService(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_sounds(new SoundPlayer(this))
{
}

NotificationService::~NotificationService() = default;

QString NotificationService::chatKey(const QString &chatId, const QString &leaf) const
{
    return QString::fromLatin1(kChatGroup) + chatId + QLatin1Char('/') + leaf;
}

bool NotificationService::notificationsEnabled() const
{
    return m_settings ? m_settings->getBool(QString::fromLatin1(kEnabledKey), true) : true;
}

void NotificationService::setNotificationsEnabled(bool enabled)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(QString::fromLatin1(kEnabledKey), enabled);
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::soundEnabled() const
{
    return m_settings ? m_settings->getBool(QString::fromLatin1(kSoundKey), true) : true;
}

void NotificationService::setSoundEnabled(bool enabled)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(QString::fromLatin1(kSoundKey), enabled);
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::previewEnabled() const
{
    return m_settings ? m_settings->getBool(QString::fromLatin1(kPreviewKey), true) : true;
}

void NotificationService::setPreviewEnabled(bool enabled)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(QString::fromLatin1(kPreviewKey), enabled);
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::chatNotificationsEnabled(const QString &chatId) const
{
    if (!m_settings || chatId.isEmpty()) {
        return true;
    }
    return m_settings->getBool(chatKey(chatId, QStringLiteral("enabled")), true);
}

void NotificationService::setChatNotificationsEnabled(const QString &chatId, bool enabled)
{
    if (!m_settings || chatId.isEmpty()) {
        return;
    }
    m_settings->setBool(chatKey(chatId, QStringLiteral("enabled")), enabled);
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::chatSoundEnabled(const QString &chatId) const
{
    if (!m_settings || chatId.isEmpty()) {
        return true;
    }
    return m_settings->getBool(chatKey(chatId, QStringLiteral("sound")), true);
}

void NotificationService::setChatSoundEnabled(const QString &chatId, bool enabled)
{
    if (!m_settings || chatId.isEmpty()) {
        return;
    }
    m_settings->setBool(chatKey(chatId, QStringLiteral("sound")), enabled);
    m_settings->flush();
    emit settingsChanged();
}

void NotificationService::setChatMutedUntil(const QString &chatId, qint64 untilMs)
{
    if (!m_settings || chatId.isEmpty()) {
        return;
    }
    if (untilMs < 0) {
        untilMs = -1; // forever
    }
    m_settings->set(chatKey(chatId, QStringLiteral("muted_until")), QString::number(untilMs));
    m_settings->flush();
    emit settingsChanged();
}

qint64 NotificationService::chatMutedUntil(const QString &chatId) const
{
    if (!m_settings || chatId.isEmpty()) {
        return 0;
    }
    bool ok = false;
    const qint64 value = m_settings->get(chatKey(chatId, QStringLiteral("muted_until")),
                                         QStringLiteral("0"))
                             .toLongLong(&ok);
    return ok ? value : 0;
}

bool NotificationService::chatMuted(const QString &chatId, const QDateTime &now) const
{
    const qint64 until = chatMutedUntil(chatId);
    if (until < 0) {
        return true; // -1 = forever
    }
    if (until == 0) {
        return false;
    }
    return now.toMSecsSinceEpoch() < until;
}

bool NotificationService::dndManual() const
{
    return m_settings ? m_settings->getBool(QString::fromLatin1(kDndManualKey), false) : false;
}

void NotificationService::setDndManual(bool on)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(QString::fromLatin1(kDndManualKey), on);
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::dndScheduleEnabled() const
{
    return m_settings ? m_settings->getBool(QString::fromLatin1(kDndScheduleKey), false) : false;
}

void NotificationService::setDndScheduleEnabled(bool on)
{
    if (!m_settings) {
        return;
    }
    m_settings->setBool(QString::fromLatin1(kDndScheduleKey), on);
    m_settings->flush();
    emit settingsChanged();
}

QTime NotificationService::dndStart() const
{
    const QString stored = m_settings ? m_settings->get(QString::fromLatin1(kDndStartKey),
                                                        QStringLiteral("23:00"))
                                      : QStringLiteral("23:00");
    return parseTime(stored, QTime(23, 0));
}

void NotificationService::setDndStart(const QTime &time)
{
    if (!m_settings || !time.isValid()) {
        return;
    }
    m_settings->set(QString::fromLatin1(kDndStartKey), timeText(time));
    m_settings->flush();
    emit settingsChanged();
}

QTime NotificationService::dndEnd() const
{
    const QString stored = m_settings ? m_settings->get(QString::fromLatin1(kDndEndKey),
                                                        QStringLiteral("07:00"))
                                      : QStringLiteral("07:00");
    return parseTime(stored, QTime(7, 0));
}

void NotificationService::setDndEnd(const QTime &time)
{
    if (!m_settings || !time.isValid()) {
        return;
    }
    m_settings->set(QString::fromLatin1(kDndEndKey), timeText(time));
    m_settings->flush();
    emit settingsChanged();
}

bool NotificationService::inQuietHours(const QTime &time, const QTime &start, const QTime &end)
{
    if (!time.isValid() || !start.isValid() || !end.isValid() || start == end) {
        return false;
    }
    if (start < end) {
        return time >= start && time < end;
    }
    // The window crosses midnight, e.g. 23:00-07:00.
    return time >= start || time < end;
}

bool NotificationService::dndActive() const
{
    return dndActiveAt(QDateTime::currentDateTime());
}

bool NotificationService::dndActiveAt(const QDateTime &now) const
{
    if (dndManual()) {
        return true;
    }
    if (!dndScheduleEnabled()) {
        return false;
    }
    return inQuietHours(now.time(), dndStart(), dndEnd());
}

void NotificationService::clearAccumulated()
{
    if (m_accumulated == 0) {
        return;
    }
    m_accumulated = 0;
    emit accumulatedChanged(0);
}

NotificationDecision NotificationService::decide(const NotificationEvent &event,
                                                 const QDateTime &now) const
{
    NotificationDecision decision;
    decision.sound = event.call ? NotificationSound::Ring
                                : (event.mention ? NotificationSound::Mention
                                                 : NotificationSound::Incoming);

    // §46.4: the master switch wins over everything.
    if (!notificationsEnabled()) {
        decision.counter = true;
        return decision;
    }

    // §46.4 per chat: notifications off means counter only.
    if (!chatNotificationsEnabled(event.chatId)) {
        decision.counter = true;
        return decision;
    }

    // §46.2 DND: nothing is shown, everything is queued. A call still
    // surfaces, but without the ringtone (§48.4: "звонок приходит как
    // уведомление, без рингтона").
    if (dndActiveAt(now)) {
        if (event.call) {
            decision.notify = true;
            decision.native = true;
            decision.showText = !event.secret && previewEnabled();
        } else {
            decision.accumulated = true;
        }
        return decision;
    }

    const bool muted = event.chatMuted || chatMuted(event.chatId, now);
    // §46.2: a muted chat only bumps the counter, but a mention always
    // surfaces ("Упоминание — всегда, отдельный звук").
    if (muted && !event.mention) {
        decision.counter = true;
        return decision;
    }

    // §46.2: reading this very chat in an active window means no bubble.
    // Mentions are the exception because the table calls them "always".
    if (event.windowActive && event.chatOpen && !event.mention) {
        return decision;
    }

    if (event.windowActive && !event.windowMinimized) {
        decision.toast = true;
    } else {
        decision.native = true;
    }
    decision.notify = decision.toast || decision.native;

    // §46.2: a secret chat is shown without any text.
    decision.showText = !event.secret && previewEnabled();
    decision.playSound = soundEnabled()
        && (event.mention || event.call || chatSoundEnabled(event.chatId));
    return decision;
}

NotificationDecision NotificationService::notify(const NotificationEvent &event)
{
    const NotificationDecision decision = decide(event);

    if (decision.counter) {
        emit counterRequested(event.chatId);
    }

    if (decision.accumulated) {
        ++m_accumulated;
        emit accumulatedChanged(m_accumulated);
    }

    if (decision.notify) {
        const QString body = decision.showText ? event.body : QString();
        if (decision.toast) {
            emit toastRequested(event.chatId, event.title, body);
        }
        if (decision.native) {
            if (m_tray && QSystemTrayIcon::supportsMessages()) {
                m_tray->showMessage(event.title, body, QSystemTrayIcon::Information, 5000);
            }
            emit nativeRequested(event.chatId, event.title, body);
        }
    }

    if (decision.playSound) {
        playSound(decision.sound);
    }
    return decision;
}

void NotificationService::playSound(NotificationSound sound)
{
    if (!soundEnabled()) {
        return;
    }
    emit soundPlayed(sound);
    if (m_sounds) {
        m_sounds->play(sound);
    }
}

void NotificationService::setSoundPlayer(SoundPlayer *player)
{
    if (m_sounds == player) {
        return;
    }
    if (m_sounds && m_sounds->parent() == this) {
        m_sounds->deleteLater();
    }
    m_sounds = player;
    if (m_sounds && !m_sounds->parent()) {
        m_sounds->setParent(this);
    }
}

} // namespace mini::services
