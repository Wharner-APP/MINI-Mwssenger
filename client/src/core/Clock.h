#pragma once

#include <QString>

namespace mini {

class Clock
{
public:
    Clock() = delete;

    static qint64 nowMs();
    static qint64 monotonicMs();
};

class Ids
{
public:
    Ids() = delete;

    static QString uuidv4();
    static QString deviceId();
    static QString clientMsgId();
};

} // namespace mini
