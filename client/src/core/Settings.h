#pragma once

#include <QString>

#include <memory>

class QSettings;

namespace mini {

// Key/value user settings persisted in config/settings.ini.
// Values are written through flush(), which the app calls on quit and after
// every change made from the settings dialog.
class Settings
{
public:
    explicit Settings(const QString &filePath = QString());
    ~Settings();

    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;

    [[nodiscard]] QString get(const QString &key, const QString &fallback = QString()) const;
    [[nodiscard]] int getInt(const QString &key, int fallback) const;
    [[nodiscard]] bool getBool(const QString &key, bool fallback) const;

    void set(const QString &key, const QString &value);
    void setInt(const QString &key, int value);
    void setBool(const QString &key, bool value);

    void flush();
    void sync();

    [[nodiscard]] QString filePath() const;

private:
    std::unique_ptr<QSettings> m_settings;
    QString m_filePath;
};

} // namespace mini
