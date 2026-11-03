#include "app/Config.h"

#include "app/Paths.h"

#include <QDir>
#include <QFile>
#include <QSettings>

namespace mini {

namespace {

QString readFirstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (line.startsWith(QChar(0xFEFF))) {
        line.remove(0, 1);
    }
    return line.trimmed();
}

} // namespace

Config::Config()
{
    reload();
}

void Config::reload()
{
    m_endpoint = readFirstLine(QDir(Paths::configDir()).filePath(QStringLiteral("connect.ip")));
    if (m_endpoint.isEmpty()) {
        m_endpoint = QStringLiteral("127.0.0.1:8765");
    }

    m_fingerprint = readFirstLine(
        QDir(Paths::configDir()).filePath(QStringLiteral("server_fingerprint.txt")));

    m_captchaSiteKey.clear();
    const QString captchaPath =
        QDir(Paths::configDir()).filePath(QStringLiteral("captcha.cfg"));
    if (QFile::exists(captchaPath)) {
        QSettings captcha(captchaPath, QSettings::IniFormat);
        m_captchaSiteKey = captcha.value(QStringLiteral("hcaptcha/site_key")).toString();
        if (m_captchaSiteKey.isEmpty()) {
            m_captchaSiteKey = captcha.value(QStringLiteral("captcha/site_key")).toString();
        }
    }
}

} // namespace mini
