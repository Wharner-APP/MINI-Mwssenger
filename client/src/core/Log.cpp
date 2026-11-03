#include "core/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace mini {

namespace {

QMutex g_mutex;
QFile g_file;
QString g_directory;
qint64 g_maxFileSize = 5 * 1024 * 1024;
int g_maxFiles = 7;
Log::Level g_minLevel = Log::Level::Info;

QString levelName(Log::Level level)
{
    switch (level) {
    case Log::Level::Trace:
        return QStringLiteral("TRACE");
    case Log::Level::Debug:
        return QStringLiteral("DEBUG");
    case Log::Level::Info:
        return QStringLiteral("INFO");
    case Log::Level::Warn:
        return QStringLiteral("WARN");
    case Log::Level::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString sanitize(QString value)
{
    value.remove(QLatin1Char('\r'));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value;
}

QString formatValue(const QVariant &value)
{
    QString text = sanitize(value.toString());
    const bool needsQuotes = text.isEmpty() || text.contains(QLatin1Char(' '))
                             || text.contains(QLatin1Char('=')) || text.contains(QLatin1Char('"'));
    if (!needsQuotes) {
        return text;
    }
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString currentPath()
{
    return QDir(g_directory).filePath(QStringLiteral("mini.log"));
}

QString rotatedPath(int index)
{
    return QDir(g_directory).filePath(QStringLiteral("mini.%1.log").arg(index));
}

void openCurrent()
{
    if (g_directory.isEmpty()) {
        return;
    }
    QDir().mkpath(g_directory);
    g_file.setFileName(currentPath());
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

} // namespace

void Log::init(const QString &directory, qint64 maxFileSize, int maxFiles)
{
    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_file.close();
    }
    g_directory = directory;
    g_maxFileSize = qMax<qint64>(256, maxFileSize);
    g_maxFiles = qMax(2, maxFiles);

    const QString env = qEnvironmentVariable("MINI_LOG_LEVEL").toLower();
    if (env == QLatin1String("trace")) {
        g_minLevel = Level::Trace;
    } else if (env == QLatin1String("debug")) {
        g_minLevel = Level::Debug;
    } else if (env == QLatin1String("warn")) {
        g_minLevel = Level::Warn;
    } else if (env == QLatin1String("error")) {
        g_minLevel = Level::Error;
    } else {
        g_minLevel = Level::Info;
    }

    openCurrent();
}

void Log::shutdown()
{
    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }
}

void Log::setMinLevel(Level level)
{
    QMutexLocker locker(&g_mutex);
    g_minLevel = level;
}

Log::Level Log::minLevel()
{
    QMutexLocker locker(&g_mutex);
    return g_minLevel;
}

void Log::trace(const QString &event, const QVariantMap &fields)
{
    write(Level::Trace, event, fields);
}

void Log::debug(const QString &event, const QVariantMap &fields)
{
    write(Level::Debug, event, fields);
}

void Log::info(const QString &event, const QVariantMap &fields)
{
    write(Level::Info, event, fields);
}

void Log::warn(const QString &event, const QVariantMap &fields)
{
    write(Level::Warn, event, fields);
}

void Log::error(const QString &event, const QVariantMap &fields)
{
    write(Level::Error, event, fields);
}

bool Log::isForbiddenField(const QString &key)
{
    static const QSet<QString> forbidden = {
        QStringLiteral("body"),
        QStringLiteral("text"),
        QStringLiteral("content"),
        QStringLiteral("message"),
        QStringLiteral("plaintext"),
        QStringLiteral("key"),
        QStringLiteral("secret"),
        QStringLiteral("password"),
        QStringLiteral("passwd"),
        QStringLiteral("sign.in"),
        QStringLiteral("signin"),
        QStringLiteral("token"),
    };
    return forbidden.contains(key.toLower());
}

QString Log::currentFile()
{
    QMutexLocker locker(&g_mutex);
    return currentPath();
}

void Log::write(Level level, const QString &event, const QVariantMap &fields)
{
    QMutexLocker locker(&g_mutex);
    if (level < g_minLevel) {
        return;
    }

    QString line = sanitize(event);
    int redacted = 0;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (isForbiddenField(it.key())) {
            ++redacted;
            continue;
        }
        line += QLatin1Char(' ') + sanitize(it.key()) + QLatin1Char('=')
                + formatValue(it.value());
    }
    if (redacted > 0) {
        line += QStringLiteral(" redacted_fields=%1").arg(redacted);
    }

    const QString record = QStringLiteral("%1 %2 %3\n")
                               .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                    levelName(level),
                                    line);

    if (!g_file.isOpen()) {
        openCurrent();
    }
    if (!g_file.isOpen()) {
        return;
    }

    if (g_file.size() + record.size() > g_maxFileSize) {
        rotate();
    }
    g_file.write(record.toUtf8());
    g_file.flush();
}

void Log::rotate()
{
    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }

    QFile::remove(rotatedPath(g_maxFiles - 1));
    for (int index = g_maxFiles - 2; index >= 1; --index) {
        const QString from = rotatedPath(index);
        if (QFile::exists(from)) {
            QFile::rename(from, rotatedPath(index + 1));
        }
    }

    const QString current = currentPath();
    if (QFile::exists(current)) {
        QFile::rename(current, rotatedPath(1));
    }
    openCurrent();
}

} // namespace mini
