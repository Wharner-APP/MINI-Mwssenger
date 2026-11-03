#include "net/ReconnectPolicy.h"

#include <QtTest>

using namespace mini::net;

class TestReconnectPolicy : public QObject
{
    Q_OBJECT

private slots:
    void baseDelaysFollowFactor();
    void jitterStaysWithinTwentyPercent();
    void resetStartsFromInitialDelay();
    void unauthorizedIsNotRetried();
    void reportsExhaustionAfterTenAttempts();
};

void TestReconnectPolicy::baseDelaysFollowFactor()
{
    QCOMPARE(ReconnectPolicy::baseDelay(1), qint64(500));
    QCOMPARE(ReconnectPolicy::baseDelay(2), qint64(900));
    QCOMPARE(ReconnectPolicy::baseDelay(3), qint64(1620));
    QCOMPARE(ReconnectPolicy::baseDelay(4), qint64(2916));
    QCOMPARE(ReconnectPolicy::baseDelay(5), qint64(5249));
    QCOMPARE(ReconnectPolicy::baseDelay(20), qint64(30000));
}

void TestReconnectPolicy::jitterStaysWithinTwentyPercent()
{
    ReconnectPolicy policy;
    policy.setSeed(42);

    for (int attempt = 1; attempt <= 10; ++attempt) {
        const qint64 base = ReconnectPolicy::baseDelay(attempt);
        const qint64 delay = policy.nextDelay();
        QVERIFY2(delay >= qMax<qint64>(0, static_cast<qint64>(base * 0.8) - 1),
                 qPrintable(QStringLiteral("attempt %1: %2 < 0.8 * %3")
                                .arg(attempt)
                                .arg(delay)
                                .arg(base)));
        QVERIFY2(delay <= qMin<qint64>(30000, static_cast<qint64>(base * 1.2) + 1),
                 qPrintable(QStringLiteral("attempt %1: %2 > 1.2 * %3")
                                .arg(attempt)
                                .arg(delay)
                                .arg(base)));
    }
}

void TestReconnectPolicy::resetStartsFromInitialDelay()
{
    ReconnectPolicy policy;
    policy.setSeed(7);
    for (int index = 0; index < 5; ++index) {
        policy.nextDelay();
    }
    QCOMPARE(policy.attempts(), 5);

    policy.reset();
    QCOMPARE(policy.attempts(), 0);
    const qint64 first = policy.nextDelay();
    QVERIFY(first >= 400 && first <= 600);
}

void TestReconnectPolicy::unauthorizedIsNotRetried()
{
    QVERIFY(!ReconnectPolicy::shouldRetryStatus(401));
    QVERIFY(ReconnectPolicy::shouldRetryStatus(500));
    QVERIFY(ReconnectPolicy::shouldRetryStatus(429));
}

void TestReconnectPolicy::reportsExhaustionAfterTenAttempts()
{
    ReconnectPolicy policy;
    policy.setSeed(1);
    for (int index = 0; index < ReconnectPolicy::kMaxAttempts; ++index) {
        QVERIFY(!policy.exhausted());
        policy.nextDelay();
    }
    QVERIFY(policy.exhausted());
}

QTEST_MAIN(TestReconnectPolicy)
#include "test_reconnect_policy.moc"
