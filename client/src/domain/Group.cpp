#include "domain/Group.h"

namespace mini {

QString groupRoleToString(GroupRole role)
{
    switch (role) {
    case GroupRole::Owner:
        return QStringLiteral("owner");
    case GroupRole::Admin:
        return QStringLiteral("admin");
    case GroupRole::Member:
        break;
    }
    return QStringLiteral("member");
}

GroupRole groupRoleFromString(const QString &value)
{
    if (value == QLatin1String("owner")) {
        return GroupRole::Owner;
    }
    if (value == QLatin1String("admin")) {
        return GroupRole::Admin;
    }
    return GroupRole::Member;
}

} // namespace mini
