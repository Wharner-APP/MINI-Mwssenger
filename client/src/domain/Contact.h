#pragma once

#include <QString>

namespace mini {

struct Contact
{
    QString login;
    QString displayName;
    bool safetyVerified = false;
    qint64 updatedAt = 0;
};

} // namespace mini
