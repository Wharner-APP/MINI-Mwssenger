#include "app/ClientRuntime.h"
#include "net/TunnelResolver.h"
#include "services/AuthService.h"
#include "services/ChatService.h"
#include "services/MessageService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace mini;
using namespace mini::services;

namespace {

QString repoRoot()
{
    return QDir(QStringLiteral(MINI_SOURCE_DIR)).absoluteFilePath(QStringLiteral("../.."));
}

QString pythonPath()
{
    const QString override = qEnvironmentVariable("MINI_E2E_PYTHON");
    if (!override.isEmpty()) {
        return override;
    }
    const QString venv =
        QDir(repoRoot()).absoluteFilePath(QStringLiteral("server/.venv/Scripts/python.exe"));
    if (QFile::exists(venv)) {
        return venv;
    }
    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

int freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const int port = probe.serverPort();
    probe.close();
    return port;
}

bool waitForPort(quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromHost();
            return true;
        }
        QTest::qWait(100);
    }
    return false;
}

} // namespace

// End-to-end: a real FastAPI server plus two real client stacks. Registration
// goes through AuthService, then two ClientRuntime instances exchange an
// encrypted text message, and a message sent while one client is offline is
// pulled from the mail buffer after it reconnects.
class TestE2E : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void twoClientsExchangeTextAndOfflineMessage();

private:
    bool registerAccount(const QString &login, const QString &name, AuthService **service,
                         RegistrationResult *result) const;

    QTemporaryDir m_dir;
    QProcess *m_server = nullptr;
    quint16 m_port = 0;
    QByteArray m_endpoint;
};

void TestE2E::initTestCase()
{
    qRegisterMetaType<mini::Message>("mini::Message");
    QVERIFY(m_dir.isValid());
    QVERIFY2(QFile::exists(pythonPath()), qPrintable(pythonPath()));

    m_port = static_cast<quint16>(freePort());
    QVERIFY(m_port != 0);
    m_endpoint = QByteArrayLiteral("http://127.0.0.1:") + QByteArray::number(m_port);

    const QString configPath = m_dir.filePath(QStringLiteral("server.toml"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&config);
    const QString root = QDir::fromNativeSeparators(m_dir.path());
    out << "[server]\nhost = \"127.0.0.1\"\nport = " << m_port << "\n"
        << "[database]\npath = \"" << root << "/server.db\"\n"
        << "[security]\nmaster_key_file = \"" << root << "/master.key\"\n"
        << "nonce_ttl_seconds = 120\nlogin_min_duration_ms = 0\n"
        << "allow_registration = true\n"
        << "[argon2]\nmemory_kib = 8192\ntime_cost = 1\nparallelism = 1\nhash_len = 32\n"
        << "[captcha]\nprovider = \"none\"\n"
        << "[logging]\ndir = \"" << root << "/logs\"\nlevel = \"warning\"\n";
    config.close();

    m_server = new QProcess();
    m_server->setWorkingDirectory(QDir(repoRoot()).absoluteFilePath(QStringLiteral("server")));
    m_server->setProcessChannelMode(QProcess::MergedChannels);
    m_server->start(pythonPath(),
                    {QStringLiteral("run.py"), QStringLiteral("--config"), configPath,
                     QStringLiteral("--no-tls"), QStringLiteral("--port"),
                     QString::number(m_port)});
    QVERIFY2(m_server->waitForStarted(10000), "server process did not start");
    QVERIFY2(waitForPort(m_port, 30000), "server did not listen");
}

void TestE2E::cleanupTestCase()
{
    if (m_server) {
        m_server->kill();
        m_server->waitForFinished(5000);
        delete m_server;
        m_server = nullptr;
    }
}

bool TestE2E::registerAccount(const QString &login, const QString &name, AuthService **service,
                              RegistrationResult *result) const
{
    const auto parsed = net::TunnelResolver::parseEndpoint(QString::fromUtf8(m_endpoint));
    if (parsed.isError()) {
        return false;
    }
    const QString dir = m_dir.filePath(login);
    QDir().mkpath(dir);
    *service = new AuthService(parsed.value(), const_cast<TestE2E *>(this));
    (*service)->setAllowInsecureTransportForTesting(true);
    (*service)->setSignInDirectory(dir);
    (*service)->setLocalDataPaths(QDir(dir).filePath(QStringLiteral("db.sqlite")),
                                  QDir(dir).filePath(QStringLiteral("keys.bin")));
    (*service)->setSignInPath(QDir(dir).filePath(QStringLiteral("sign.in")));

    bool done = false;
    (*service)->registerAccount(login, name, QStringLiteral("Password123"), QString(),
                                [&](Result<RegistrationResult> value) {
        done = value.isOk();
        if (done) {
            *result = value.value();
        }
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    return done;
}

void TestE2E::twoClientsExchangeTextAndOfflineMessage()
{
    RegistrationResult aliceRegistration;
    RegistrationResult bobRegistration;
    AuthService *aliceAuth = nullptr;
    AuthService *bobAuth = nullptr;
    QVERIFY(registerAccount(QStringLiteral("alice_e2e"), QStringLiteral("Alice"), &aliceAuth,
                            &aliceRegistration));
    QVERIFY(registerAccount(QStringLiteral("bob_e2e"), QStringLiteral("Bob"), &bobAuth,
                            &bobRegistration));

    const auto endpoint = net::TunnelResolver::parseEndpoint(QString::fromUtf8(m_endpoint));
    QVERIFY(endpoint.isOk());

    ClientRuntime::Config aliceConfig;
    aliceConfig.endpoint = endpoint.value();
    aliceConfig.dataDir = m_dir.filePath(QStringLiteral("alice_data"));
    aliceConfig.accessToken = aliceRegistration.session.accessToken;
    aliceConfig.refreshToken = aliceRegistration.session.refreshToken;
    aliceConfig.deviceId = aliceRegistration.session.deviceId;
    aliceConfig.selfLogin = aliceRegistration.session.login;
    aliceConfig.kenc = aliceAuth->fileKeys().kenc;

    ClientRuntime::Config bobConfig = aliceConfig;
    bobConfig.dataDir = m_dir.filePath(QStringLiteral("bob_data"));
    bobConfig.accessToken = bobRegistration.session.accessToken;
    bobConfig.refreshToken = bobRegistration.session.refreshToken;
    bobConfig.deviceId = bobRegistration.session.deviceId;
    bobConfig.selfLogin = bobRegistration.session.login;
    bobConfig.kenc = bobAuth->fileKeys().kenc;

    ClientRuntime alice;
    ClientRuntime bob;
    const auto aliceStart = alice.start(aliceConfig);
    QVERIFY2(aliceStart.isOk(), qPrintable(aliceStart.error()));
    const auto bobStart = bob.start(bobConfig);
    QVERIFY2(bobStart.isOk(), qPrintable(bobStart.error()));
    alice.messageService()->setCancelWindowMs(0);
    bob.messageService()->setCancelWindowMs(0);

    QTRY_VERIFY_WITH_TIMEOUT(alice.hasPublishedKeys(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(bob.hasPublishedKeys(), 15000);

    bool chatReady = false;
    QString chatId;
    alice.chatService()->openDirectChat(QStringLiteral("bob_e2e"),
                                        [&](Result<Chat> result) {
        chatReady = result.isOk();
        if (chatReady) {
            chatId = result.value().chatId;
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(chatReady, 10000);

    QSignalSpy bobAdded(bob.messageService(), &MessageService::messageAdded);
    alice.messageService()->sendText(chatId, QStringLiteral("hello from alice"));
    QTRY_VERIFY_WITH_TIMEOUT(bobAdded.count() >= 1, 20000);
    bool firstSeen = false;
    for (const QList<QVariant> &args : bobAdded) {
        const Message message = args.at(0).value<Message>();
        firstSeen = firstSeen || message.body == QStringLiteral("hello from alice");
    }
    QVERIFY(firstSeen);

    // Offline: Bob disconnects, Alice sends, Bob reconnects and pulls the mail.
    bob.stop();
    alice.messageService()->sendText(chatId, QStringLiteral("while you were away"));
    QTest::qWait(1000);

    const auto bobRestart = bob.start(bobConfig);
    QVERIFY2(bobRestart.isOk(), qPrintable(bobRestart.error()));
    QSignalSpy bobAddedAgain(bob.messageService(), &MessageService::messageAdded);
    QTRY_VERIFY_WITH_TIMEOUT(bobAddedAgain.count() >= 1, 30000);
    bool offlineSeen = false;
    for (const QList<QVariant> &args : bobAddedAgain) {
        const Message message = args.at(0).value<Message>();
        offlineSeen = offlineSeen || message.body == QStringLiteral("while you were away");
    }
    QVERIFY(offlineSeen);

    alice.stop();
    bob.stop();
}

QTEST_MAIN(TestE2E)
#include "test_e2e.moc"
