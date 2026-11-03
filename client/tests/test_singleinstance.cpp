#include "app/SingleInstance.h"

#include <QUuid>
#include <QtTest>

using namespace mini;

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void secondInstanceIsRejected();
};

void TestSingleInstance::secondInstanceIsRejected()
{
    const QString key =
        QStringLiteral("mini-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);

    {
        SingleInstance first(key);
        QVERIFY2(first.tryLock(), "first instance must acquire the channel");

        SingleInstance second(key);
        QVERIFY2(!second.tryLock(), "second instance must not acquire the channel");
    }

    SingleInstance afterRelease(key);
    QVERIFY2(afterRelease.tryLock(), "lock must be released when the owner exits");
}

QTEST_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
