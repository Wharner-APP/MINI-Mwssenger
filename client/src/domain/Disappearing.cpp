#include "domain/Disappearing.h"

#include <QCoreApplication>

namespace mini::disappearing {

QVector<qint64> values()
{
    return {0, kMinute, kFiveMinutes, kHour, kDay, kWeek};
}

bool isValid(qint64 milliseconds)
{
    for (const qint64 value : values()) {
        if (value == milliseconds) {
            return true;
        }
    }
    return false;
}

QString label(qint64 milliseconds)
{
    switch (milliseconds) {
    case 0:
        return QCoreApplication::translate("Disappearing", "Off");
    case kMinute:
        return QCoreApplication::translate("Disappearing", "1 minute");
    case kFiveMinutes:
        return QCoreApplication::translate("Disappearing", "5 minutes");
    case kHour:
        return QCoreApplication::translate("Disappearing", "1 hour");
    case kDay:
        return QCoreApplication::translate("Disappearing", "1 day");
    case kWeek:
        return QCoreApplication::translate("Disappearing", "1 week");
    default:
        return {};
    }
}

} // namespace mini::disappearing
