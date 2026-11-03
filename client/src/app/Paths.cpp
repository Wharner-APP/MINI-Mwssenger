#include "app/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace mini {

QString Paths::s_root;
QString Paths::s_rootOverride;
bool Paths::s_initialized = false;

namespace {

bool isDirectoryWritable(const QString &dir)
{
    QDir().mkpath(dir);
    QFile probe(QDir(dir).filePath(QStringLiteral(".mini-write-probe")));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    probe.close();
    probe.remove();
    return true;
}

} // namespace

void Paths::initialize()
{
    if (s_initialized) {
        return;
    }

    if (!s_rootOverride.isEmpty()) {
        s_root = s_rootOverride;
    } else {
        const QString portable = appDir();
        if (isDirectoryWritable(portable)) {
            s_root = portable;
        } else {
            s_root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                         .filePath(QStringLiteral("MINIMessenger"));
        }
    }

    s_initialized = true;

    for (const QString &dir : {configDir(), dataDir(), logsDir(), mediaDir(), backupsDir()}) {
        QDir().mkpath(dir);
    }
}

void Paths::setRootOverride(const QString &root)
{
    s_rootOverride = QDir(root).absolutePath();
    s_initialized = false;
    s_root.clear();
}

void Paths::clearRootOverride()
{
    s_rootOverride.clear();
    s_initialized = false;
    s_root.clear();
}

QString Paths::appDir()
{
    return QCoreApplication::applicationDirPath();
}

QString Paths::root()
{
    initialize();
    return s_root;
}

QString Paths::configDir()
{
    const QString env = qEnvironmentVariable("MINI_CONFIG_DIR");
    if (!env.isEmpty()) {
        QDir().mkpath(env);
        return env;
    }
    return QDir(root()).filePath(QStringLiteral("config"));
}

QString Paths::dataDir()
{
    return QDir(root()).filePath(QStringLiteral("data"));
}

QString Paths::logsDir()
{
    // Ticket 01 acceptance writes logs to data/logs.
    return QDir(dataDir()).filePath(QStringLiteral("logs"));
}

QString Paths::mediaDir()
{
    return QDir(root()).filePath(QStringLiteral("media"));
}

QString Paths::backupsDir()
{
    return QDir(root()).filePath(QStringLiteral("backups"));
}

QString Paths::translationsDir()
{
    return QDir(appDir()).filePath(QStringLiteral("translations"));
}

} // namespace mini
