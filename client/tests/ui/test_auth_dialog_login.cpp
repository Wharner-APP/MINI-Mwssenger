// Regression: signing in through AuthDialog must reach the account page
// without crashing. The dialog talks to a real HTTPS server, so this also
// covers the Qt TLS backend end to end.

#include "app/Paths.h"
#include "core/BuildInfo.h"
#include "core/Result.h"
#include "core/Settings.h"
#include "net/TlsPin.h"
#include "net/TunnelResolver.h"
#include "services/AuthService.h"
#include "ui/AuthDialog.h"
#include "ui/Theme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace mini;

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
    return QString();
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

QLineEdit *editWithPlaceholder(QWidget *root, const QString &needle)
{
    const auto edits = root->findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits) {
        if (edit->isVisibleTo(root)
            && edit->placeholderText().contains(needle, Qt::CaseInsensitive)) {
            return edit;
        }
    }
    return nullptr;
}

QPushButton *visibleButton(QWidget *root, const QString &text)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->isVisibleTo(root) && button->isEnabled() && button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QString readLineWithPrefix(const QByteArray &data, const QByteArray &prefix)
{
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().startsWith(prefix)) {
            return QString::fromUtf8(line.mid(prefix.size())).trimmed();
        }
    }
    return {};
}

} // namespace

class TestAuthDialogLogin : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void loginReachesAccountPageOverHttps();

private:
    QTemporaryDir m_dir;
    QProcess *m_server = nullptr;
    quint16 m_port = 0;
    QString m_fingerprint;
    QString m_signInPath;
    QString m_deviceId;
};

void TestAuthDialogLogin::initTestCase()
{
    QVERIFY(m_dir.isValid());
    Resources::initialize();
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                            Density::Comfortable);

    const QString python = pythonPath();
    if (python.isEmpty()) {
        QSKIP("server/.venv is not available");
    }

    m_port = static_cast<quint16>(freePort());
    QVERIFY(m_port != 0);

    const QString root = QDir::fromNativeSeparators(m_dir.path());
    const QString configPath = m_dir.filePath(QStringLiteral("server.toml"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&config);
    out << "[server]\nhost = \"127.0.0.1\"\nport = " << m_port << "\n"
        << "allow_registration = true\n"
        << "[database]\npath = \"" << root << "/server.db\"\n"
        << "[security]\nmaster_key_file = \"" << root << "/master.key\"\n"
        << "nonce_ttl_seconds = 120\nlogin_min_duration_ms = 0\n"
        << "[argon2]\nmemory_kib = 8192\ntime_cost = 1\nparallelism = 1\nhash_len = 32\n"
        << "[captcha]\nprovider = \"none\"\n"
        << "[tls]\ncert = \"" << root << "/server.pem\"\nkey = \"" << root
        << "/server.key\"\n"
        << "[logging]\ndir = \"" << root << "/logs\"\nlevel = \"warning\"\n";
    config.close();

    // --print-fingerprint also creates the certificate; the process exits, so
    // its buffered stdout is flushed and can be parsed reliably.
    QProcess fingerprintProcess;
    fingerprintProcess.setWorkingDirectory(
        QDir(repoRoot()).absoluteFilePath(QStringLiteral("server")));
    fingerprintProcess.start(python, {QStringLiteral("run.py"), QStringLiteral("--config"),
                                      configPath, QStringLiteral("--print-fingerprint")});
    QVERIFY2(fingerprintProcess.waitForStarted(10000), "fingerprint process did not start");
    QVERIFY2(fingerprintProcess.waitForFinished(30000), "fingerprint process hung");
    const QByteArray fingerprintOutput =
        fingerprintProcess.readAllStandardOutput() + fingerprintProcess.readAllStandardError();
    m_fingerprint = readLineWithPrefix(fingerprintOutput,
                                       QByteArrayLiteral("CURRENT FINGERPRINT:"));
    QVERIFY2(!m_fingerprint.isEmpty(), qPrintable(QString::fromUtf8(fingerprintOutput)));

    m_server = new QProcess();
    m_server->setWorkingDirectory(QDir(repoRoot()).absoluteFilePath(QStringLiteral("server")));
    m_server->setProcessChannelMode(QProcess::MergedChannels);
    m_server->start(python, {QStringLiteral("run.py"), QStringLiteral("--config"), configPath,
                             QStringLiteral("--port"), QString::number(m_port)});
    QVERIFY2(m_server->waitForStarted(10000), "server process did not start");
    QVERIFY2(waitForPort(m_port, 30000), "server did not listen");

    Paths::setRootOverride(m_dir.filePath(QStringLiteral("client")));
    QDir().mkpath(Paths::configDir());
    {
        QFile endpoint(QDir(Paths::configDir()).filePath(QStringLiteral("connect.ip")));
        QVERIFY(endpoint.open(QIODevice::WriteOnly | QIODevice::Text));
        endpoint.write(QStringLiteral("https://127.0.0.1:%1").arg(m_port).toUtf8());
    }
    {
        QFile pin(QDir(Paths::configDir()).filePath(QStringLiteral("server_fingerprint.txt")));
        QVERIFY(pin.open(QIODevice::WriteOnly | QIODevice::Text));
        pin.write(m_fingerprint.toUtf8() + '\n');
    }

    const auto parsed = net::TunnelResolver::parseEndpoint(
        QStringLiteral("https://127.0.0.1:%1").arg(m_port));
    QVERIFY(parsed.isOk());
    net::TlsPin pinValue(m_fingerprint);
    services::AuthService registrar(parsed.value());
    registrar.setTlsPin(&pinValue);
    registrar.setSignInDirectory(m_dir.filePath(QStringLiteral("signin")));
    registrar.setLocalDataPaths(
        QDir(Paths::dataDir()).filePath(QStringLiteral("registrar.db")),
        QDir(Paths::dataDir()).filePath(QStringLiteral("registrar-keys.bin")));

    bool registered = false;
    services::RegistrationResult registration;
    registrar.registerAccount(QStringLiteral("blind_login"),
                              QStringLiteral("Blind Login"),
                              QStringLiteral("Password123"), QString(),
                              [&](Result<services::RegistrationResult> result) {
        registered = result.isOk();
        if (registered) {
            registration = result.value();
        }
    });
    QElapsedTimer timer;
    timer.start();
    while (!registered && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    QVERIFY(registered);
    m_signInPath = registration.filePath;
    m_deviceId = registration.session.deviceId;
    QVERIFY(QFile::exists(m_signInPath));

    Settings settings;
    settings.set(QStringLiteral("auth/device_id"), m_deviceId);
    settings.set(QStringLiteral("auth/last_signin_path"), m_signInPath);
    settings.flush();
}

void TestAuthDialogLogin::cleanupTestCase()
{
    Paths::clearRootOverride();
    if (m_server) {
        m_server->kill();
        m_server->waitForFinished(5000);
        delete m_server;
        m_server = nullptr;
    }
}

void TestAuthDialogLogin::loginReachesAccountPageOverHttps()
{
    AuthDialog dialog;
    dialog.loginButton()->click();

    QLineEdit *login = editWithPlaceholder(&dialog, QStringLiteral("Login"));
    QLineEdit *password = editWithPlaceholder(&dialog, QStringLiteral("Password"));
    QLineEdit *signIn = editWithPlaceholder(&dialog, QStringLiteral("sign.in"));
    QVERIFY(login != nullptr);
    QVERIFY(password != nullptr);
    QVERIFY(signIn != nullptr);

    login->setText(QStringLiteral("blind_login"));
    password->setText(QStringLiteral("Password123"));
    signIn->setText(m_signInPath);

    QPushButton *submit = visibleButton(&dialog, QStringLiteral("Log in"));
    QVERIFY(submit != nullptr);
    submit->click();

    QElapsedTimer timer;
    timer.start();
    while (!dialog.hasSession() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    QVERIFY2(dialog.hasSession(), "login did not complete");

    // The successful login handler opens the account page and refreshes the
    // device list; let those callbacks run.
    QTest::qWait(1500);

    auto *pages = dialog.findChild<QStackedWidget *>();
    QVERIFY(pages != nullptr);
    QCOMPARE(pages->currentIndex(), 4);
    QVERIFY(dialog.hasSession());

    // The same client may sign out and sign in again without restarting; the
    // service persists across that cycle.
    bool loggedOut = false;
    dialog.authService()->logout([&](Result<bool> result) {
        loggedOut = result.isOk();
    });
    QElapsedTimer logoutTimer;
    logoutTimer.start();
    while (!loggedOut && logoutTimer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    QVERIFY(loggedOut);
    QVERIFY(!dialog.hasSession());

    dialog.showWelcomePage();
    dialog.loginButton()->click();
    login = editWithPlaceholder(&dialog, QStringLiteral("Login"));
    password = editWithPlaceholder(&dialog, QStringLiteral("Password"));
    signIn = editWithPlaceholder(&dialog, QStringLiteral("sign.in"));
    QVERIFY(login != nullptr);
    QVERIFY(password != nullptr);
    QVERIFY(signIn != nullptr);
    login->setText(QStringLiteral("blind_login"));
    password->setText(QStringLiteral("Password123"));
    signIn->setText(m_signInPath);
    submit = visibleButton(&dialog, QStringLiteral("Log in"));
    QVERIFY(submit != nullptr);
    submit->click();

    QElapsedTimer secondTimer;
    secondTimer.start();
    while (!dialog.hasSession() && secondTimer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    QVERIFY2(dialog.hasSession(), "second login did not complete");
    QTest::qWait(1000);
    QCOMPARE(pages->currentIndex(), 4);
}

QTEST_MAIN(TestAuthDialogLogin)
#include "test_auth_dialog_login.moc"
