#pragma once

#include <QRandomGenerator>

namespace mini::net {

// Exponential backoff: 500 ms, factor 1.8, cap 30 s, jitter +/-20%.
// HTTP 401 is never retried; after 10 failed attempts the UI offers
// "Проверить подключение".
class ReconnectPolicy
{
public:
    static constexpr int kMaxAttempts = 10;
    static constexpr qint64 kInitialDelayMs = 500;
    static constexpr qint64 kMaxDelayMs = 30000;

    ReconnectPolicy(qint64 initialMs = kInitialDelayMs, qint64 maxMs = kMaxDelayMs,
                    double factor = 1.8, double jitter = 0.2);

    // Next delay with jitter and advances the attempt counter.
    qint64 nextDelay();
    // Base delay for 1-based attempt without jitter.
    [[nodiscard]] static qint64 baseDelay(int attempt, qint64 initialMs = kInitialDelayMs,
                                          qint64 maxMs = kMaxDelayMs, double factor = 1.8);

    void reset();
    [[nodiscard]] int attempts() const { return m_attempt; }
    [[nodiscard]] bool exhausted() const { return m_attempt >= kMaxAttempts; }

    [[nodiscard]] static bool shouldRetryStatus(int httpStatus);

    // Deterministic jitter for tests; 0 restores the system generator.
    void setSeed(quint32 seed);

private:
    qint64 m_initialMs;
    qint64 m_maxMs;
    double m_factor;
    double m_jitter;
    int m_attempt = 0;
    QRandomGenerator m_random;
};

} // namespace mini::net
