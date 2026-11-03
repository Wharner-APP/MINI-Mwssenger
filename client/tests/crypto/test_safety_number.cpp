#include "crypto/SafetyNumber.h"

#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

using mini::Result;
using mini::crypto::SafetyNumber;
using mini::crypto::SafetyNumberCode;

namespace {

QByteArray identity(char seed)
{
    QByteArray key(32, Qt::Uninitialized);
    for (int i = 0; i < key.size(); ++i) {
        key[i] = static_cast<char>(seed + i);
    }
    return key;
}

} // namespace

class TestSafetyNumber : public QObject
{
    Q_OBJECT

private slots:
    void twelveGroupsOfFiveDigits();
    void bothSidesComputeTheSameNumber();
    void identityChangeChangesEveryNumber();
    void identityChangedDetectsRotation();
    void badIdentitySizesAreRejected();
};

void TestSafetyNumber::twelveGroupsOfFiveDigits()
{
    const Result<SafetyNumberCode> code =
        SafetyNumber::compute(QStringLiteral("alice"), QStringLiteral("bob"),
                              identity(1), identity(101));
    QVERIFY2(code.isOk(), qPrintable(code.error()));
    QVERIFY(code.value().isValid());
    QCOMPARE(code.value().digits.size(), 60);

    const QStringList groups = code.value().formatted.split(QLatin1Char(' '));
    QCOMPARE(groups.size(), SafetyNumberCode::kGroupCount);
    const QRegularExpression groupPattern(QStringLiteral("^[0-9]{5}$"));
    for (const QString &group : groups) {
        QVERIFY2(groupPattern.match(group).hasMatch(), qPrintable(group));
    }
    QCOMPARE(code.value().digits, groups.join(QString()));
}

void TestSafetyNumber::bothSidesComputeTheSameNumber()
{
    const QByteArray aliceIdentity = identity(7);
    const QByteArray bobIdentity = identity(77);

    const SafetyNumberCode forward =
        SafetyNumber::compute(QStringLiteral("alice"), QStringLiteral("bob"),
                              aliceIdentity, bobIdentity)
            .value();
    const SafetyNumberCode backward =
        SafetyNumber::compute(QStringLiteral("bob"), QStringLiteral("alice"),
                              bobIdentity, aliceIdentity)
            .value();
    QVERIFY(forward == backward);
    QCOMPARE(forward.formatted, backward.formatted);
}

void TestSafetyNumber::identityChangeChangesEveryNumber()
{
    const QByteArray aliceIdentity = identity(3);
    const QByteArray bobIdentity = identity(90);
    const QByteArray bobRotated = identity(90);
    QByteArray rotated = bobRotated;
    rotated[0] = static_cast<char>(rotated.at(0) ^ 0x01);

    const SafetyNumberCode before =
        SafetyNumber::compute(QStringLiteral("alice"), QStringLiteral("bob"),
                              aliceIdentity, bobIdentity)
            .value();
    const SafetyNumberCode after =
        SafetyNumber::compute(QStringLiteral("alice"), QStringLiteral("bob"),
                              aliceIdentity, rotated)
            .value();
    QVERIFY(before != after);
    QVERIFY(before.digits != after.digits);
}

void TestSafetyNumber::identityChangedDetectsRotation()
{
    const QByteArray stored = identity(10);
    const QByteArray current = identity(11);
    QVERIFY(!SafetyNumber::identityChanged(stored, stored));
    QVERIFY(SafetyNumber::identityChanged(stored, current));
    QVERIFY(!SafetyNumber::identityChanged(QByteArray(), current));
}

void TestSafetyNumber::badIdentitySizesAreRejected()
{
    QVERIFY(SafetyNumber::compute(QStringLiteral("a"), QStringLiteral("b"),
                                  QByteArray(31, '\x01'), identity(2))
                .isError());
    QVERIFY(SafetyNumber::compute(QStringLiteral("a"), QStringLiteral("b"), identity(2),
                                  QByteArray())
                .isError());
}

QTEST_MAIN(TestSafetyNumber)
#include "test_safety_number.moc"
