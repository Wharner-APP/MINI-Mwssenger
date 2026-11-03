#include "app/Paths.h"
#include "services/SignInService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using mini::Result;
using mini::crypto::NormalizedSignIn;
using mini::crypto::SignInKey;
using mini::crypto::SignInMaterial;
using mini::services::SignInService;

class TestSignInService : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void savesUniqueNamesInDownloads();
    void fallsBackToDataDirectory();
    void missingFileReportsMissing();
    void replaceFileOverwritesAtomically();

private:
    QTemporaryDir *m_temp = nullptr;
    QString m_downloads;
};

void TestSignInService::init()
{
    m_temp = new QTemporaryDir();
    QVERIFY(m_temp->isValid());
    m_downloads = QDir(m_temp->path()).filePath(QStringLiteral("Downloads"));
    QVERIFY(QDir().mkpath(m_downloads));
    mini::Paths::setRootOverride(QDir(m_temp->path()).filePath(QStringLiteral("root")));
}

void TestSignInService::cleanup()
{
    mini::Paths::clearRootOverride();
    delete m_temp;
    m_temp = nullptr;
}

void TestSignInService::savesUniqueNamesInDownloads()
{
    SignInService service(m_downloads);
    const Result<SignInMaterial> first = service.create();
    const Result<SignInMaterial> second = service.create();
    QVERIFY(first.isOk());
    QVERIFY(second.isOk());

    bool fallback = true;
    const Result<QString> firstPath = service.save(first.value(), &fallback);
    QVERIFY2(firstPath.isOk(), qPrintable(firstPath.error()));
    QVERIFY(!fallback);
    QCOMPARE(QFileInfo(firstPath.value()).fileName(), QStringLiteral("sign.in"));

    const Result<QString> secondPath = service.save(second.value(), &fallback);
    QVERIFY2(secondPath.isOk(), qPrintable(secondPath.error()));
    QCOMPARE(QFileInfo(secondPath.value()).fileName(), QStringLiteral("sign (2).in"));

    const Result<NormalizedSignIn> parsed = service.read(secondPath.value());
    QVERIFY2(parsed.isOk(), qPrintable(parsed.error()));
    QCOMPARE(parsed.value().normalized, second.value().parsed.normalized);
}

void TestSignInService::fallsBackToDataDirectory()
{
    QFile blocker(QDir(m_temp->path()).filePath(QStringLiteral("blocker")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    SignInService service(QDir(m_temp->path()).filePath(QStringLiteral("blocker/sub")));
    const Result<SignInMaterial> material = service.create();
    QVERIFY(material.isOk());

    bool fallback = false;
    const Result<QString> path = service.save(material.value(), &fallback);
    QVERIFY2(path.isOk(), qPrintable(path.error()));
    QVERIFY(fallback);
    QVERIFY(path.value().startsWith(mini::Paths::dataDir()));
    QVERIFY(QFileInfo::exists(path.value()));
    QVERIFY(service.read(path.value()).isOk());
}

void TestSignInService::missingFileReportsMissing()
{
    SignInService service(m_downloads);
    const Result<NormalizedSignIn> parsed =
        service.read(QDir(m_downloads).filePath(QStringLiteral("absent.in")));
    QVERIFY(parsed.isError());
    QCOMPARE(parsed.error(), QStringLiteral("signin.missing"));
}

void TestSignInService::replaceFileOverwritesAtomically()
{
    SignInService service(m_downloads);
    const SignInMaterial first = service.create().value();
    const SignInMaterial second = service.create().value();
    const QString target = QDir(m_downloads).filePath(QStringLiteral("sign.in"));
    const QString temporary = target + QStringLiteral(".new");

    QVERIFY(service.saveTo(first, target).isOk());
    QVERIFY(service.saveTo(second, temporary).isOk());
    const Result<QString> replaced = service.replaceFile(temporary, target);
    QVERIFY2(replaced.isOk(), qPrintable(replaced.error()));
    QVERIFY(!QFileInfo::exists(temporary));

    const Result<NormalizedSignIn> parsed = service.read(target);
    QVERIFY(parsed.isOk());
    QCOMPARE(parsed.value().normalized, second.parsed.normalized);
}

QTEST_GUILESS_MAIN(TestSignInService)
#include "test_signin_service.moc"
