#pragma once

#include <QString>

namespace mini {

// Resolves the portable layout next to the executable:
//   config/ data/ logs/ media/ backups/
// If the executable directory is not writable, %APPDATA%/MINIMessenger is used.
class Paths
{
public:
    Paths() = delete;

    static void initialize();

    // Test seam: use an explicit root instead of the executable directory.
    static void setRootOverride(const QString &root);
    static void clearRootOverride();

    [[nodiscard]] static QString appDir();
    [[nodiscard]] static QString configDir();
    [[nodiscard]] static QString dataDir();
    [[nodiscard]] static QString logsDir();
    [[nodiscard]] static QString mediaDir();
    [[nodiscard]] static QString backupsDir();
    [[nodiscard]] static QString translationsDir();

private:
    static QString root();
    static QString s_root;
    static QString s_rootOverride;
    static bool s_initialized;
};

} // namespace mini
