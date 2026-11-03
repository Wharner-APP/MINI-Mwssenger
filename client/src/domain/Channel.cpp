#include "domain/Channel.h"

namespace mini {

QString channelRoleToString(ChannelRole role)
{
    switch (role) {
    case ChannelRole::Owner:
        return QStringLiteral("owner");
    case ChannelRole::Admin:
        return QStringLiteral("admin");
    case ChannelRole::Subscriber:
        break;
    }
    return QStringLiteral("subscriber");
}

ChannelRole channelRoleFromString(const QString &value)
{
    if (value == QLatin1String("owner")) {
        return ChannelRole::Owner;
    }
    if (value == QLatin1String("admin")) {
        return ChannelRole::Admin;
    }
    return ChannelRole::Subscriber;
}

} // namespace mini
