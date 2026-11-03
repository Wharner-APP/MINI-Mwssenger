#pragma once

#include <QString>
#include <QVariantMap>

namespace mini {

// Rolling text log. Only structured events and scalar fields are accepted;
// message bodies, keys, passwords and sign-in material are dropped.
class Log
{
public:
    enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

    Log() = delete;

    static void init(const QString &directory,
                     qint64 maxFileSize = 5 * 1024 * 1024,
                     int maxFiles = 7);
    static void shutdown();

    static void setMinLevel(Level level);
    [[nodiscard]] static Level minLevel();

    static void trace(const QString &event, const QVariantMap &fields = {});
    static void debug(const QString &event, const QVariantMap &fields = {});
    static void info(const QString &event, const QVariantMap &fields = {});
    static void warn(const QString &event, const QVariantMap &fields = {});
    static void error(const QString &event, const QVariantMap &fields = {});

    [[nodiscard]] static bool isForbiddenField(const QString &key);
    [[nodiscard]] static QString currentFile();

private:
    static void write(Level level, const QString &event, const QVariantMap &fields);
    static void rotate();
};

} // namespace mini
