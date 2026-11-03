#include "net/ReconnectPolicy.h"

#include <algorithm>
#include <cmath>

namespace mini::net {

ReconnectPolicy::ReconnectPolicy(qint64 initialMs, qint64 maxMs, double factor, double jitter)
    : m_initialMs(initialMs)
    , m_maxMs(maxMs)
    , m_factor(factor)
    , m_jitter(jitter)
    , m_random(QRandomGenerator::system()->generate())
{
}

qint64 ReconnectPolicy::baseDelay(int attempt, qint64 initialMs, qint64 maxMs, double factor)
{
    if (attempt <= 1) {
        return initialMs;
    }
    const double value = static_cast<double>(initialMs)
        * std::pow(factor, static_cast<double>(attempt - 1));
    if (value >= static_cast<double>(maxMs)) {
        return maxMs;
    }
    return static_cast<qint64>(std::llround(value));
}

qint64 ReconnectPolicy::nextDelay()
{
    ++m_attempt;
    const qint64 base = baseDelay(m_attempt, m_initialMs, m_maxMs, m_factor);

    const double randomUnit = static_cast<double>(m_random.generate()) / 4294967295.0;
    const double scale = 1.0 + m_jitter * (randomUnit * 2.0 - 1.0);
    const qint64 jittered = static_cast<qint64>(std::llround(static_cast<double>(base) * scale));
    return std::clamp<qint64>(jittered, 0, m_maxMs);
}

void ReconnectPolicy::reset()
{
    m_attempt = 0;
}

bool ReconnectPolicy::shouldRetryStatus(int httpStatus)
{
    return httpStatus != 401;
}

void ReconnectPolicy::setSeed(quint32 seed)
{
    m_random.seed(seed);
}

} // namespace mini::net
