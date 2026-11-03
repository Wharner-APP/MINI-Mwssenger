#include "core/BuildInfo.h"

#include <QResource>
#include <QtGlobal>

#include <sodium.h>
#include <sqlite3.h>

#ifndef MINI_APP_VERSION
#define MINI_APP_VERSION "0.1.0"
#endif

#ifndef MINI_BUILD_TYPE
#define MINI_BUILD_TYPE "Release"
#endif

// Q_INIT_RESOURCE must be called from a function in the global namespace,
// otherwise the generated symbol is looked up inside the class scope.
static void miniInitializeResourceArchive()
{
    Q_INIT_RESOURCE(app_resources);
}

namespace mini {

QString BuildInfo::appVersion()
{
    return QStringLiteral(MINI_APP_VERSION);
}

QString BuildInfo::qtVersion()
{
    return QString::fromLatin1(qVersion());
}

QString BuildInfo::sodiumVersion()
{
    return QString::fromLatin1(sodium_version_string());
}

QString BuildInfo::sqliteVersion()
{
    return QString::fromLatin1(sqlite3_libversion());
}

QString BuildInfo::buildType()
{
    return QStringLiteral(MINI_BUILD_TYPE);
}

bool BuildInfo::initializeCrypto()
{
    return sodium_init() >= 0;
}

void Resources::initialize()
{
    static bool done = false;
    if (done) {
        return;
    }
    miniInitializeResourceArchive();
    done = true;
}

} // namespace mini
