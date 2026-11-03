#include "core/Settings.h"

#include "app/Paths.h"

#include <QDir>
#include <QSettings>

namespace mini {

Settings::Settings(const QString &filePath)
    : m_filePath(filePath)
{
    if (m_filePath.isEmpty()) {
        QDir().mkpath(Paths::configDir());
        m_filePath = QDir(Paths::configDir()).filePath(QStringLiteral("settings.ini"));
    }
    m_settings = std::make_unique<QSettings>(m_filePath, QSettings::IniFormat);
}

Settings::~Settings() = default;

QString Settings::get(const QString &key, const QString &fallback) const
{
    return m_settings->value(key, fallback).toString();
}

int Settings::getInt(const QString &key, int fallback) const
{
    bool ok = false;
    const int value = m_settings->value(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

bool Settings::getBool(const QString &key, bool fallback) const
{
    return m_settings->value(key, fallback).toBool();
}

void Settings::set(const QString &key, const QString &value)
{
    m_settings->setValue(key, value);
}

void Settings::setInt(const QString &key, int value)
{
    m_settings->setValue(key, value);
}

void Settings::setBool(const QString &key, bool value)
{
    m_settings->setValue(key, value);
}

void Settings::flush()
{
    m_settings->sync();
}

void Settings::sync()
{
    m_settings->sync();
}

QString Settings::filePath() const
{
    return m_filePath;
}

} // namespace mini
