#include "core/Clock.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QUuid>

namespace mini {

qint64 Clock::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 Clock::monotonicMs()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

QString Ids::uuidv4()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString Ids::deviceId()
{
    return uuidv4();
}

QString Ids::clientMsgId()
{
    return uuidv4();
}

} // namespace mini
