#pragma once

#include <QString>
#include <QVector>

namespace mini::disappearing {

// §39.1: off, 1 minute, 5 minutes, 1 hour, 1 day, 1 week.
[[nodiscard]] QVector<qint64> values();
[[nodiscard]] bool isValid(qint64 milliseconds);
// Localized label used by the header, the menu and the service message.
[[nodiscard]] QString label(qint64 milliseconds);

constexpr qint64 kMinute = 60LL * 1000;
constexpr qint64 kFiveMinutes = 5LL * 60 * 1000;
constexpr qint64 kHour = 60LL * 60 * 1000;
constexpr qint64 kDay = 24LL * 60 * 60 * 1000;
constexpr qint64 kWeek = 7LL * 24 * 60 * 60 * 1000;

} // namespace mini::disappearing
