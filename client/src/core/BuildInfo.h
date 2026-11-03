#pragma once

#include <QString>

namespace mini {

class BuildInfo
{
public:
    BuildInfo() = delete;

    [[nodiscard]] static QString appVersion();
    [[nodiscard]] static QString qtVersion();
    [[nodiscard]] static QString sodiumVersion();
    [[nodiscard]] static QString sqliteVersion();
    [[nodiscard]] static QString buildType();

    // sodium_init() must run before any crypto call.
    static bool initializeCrypto();
};

class Resources
{
public:
    Resources() = delete;

    // Pulls the compiled Qt resource archive into the binary. Explicit because
    // the resources live in a static library.
    static void initialize();
};

} // namespace mini
