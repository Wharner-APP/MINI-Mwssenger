#include "services/AuthService.h"

#include "crypto/IdentityKeys.h"
#include "crypto/Kdf.h"
#include "storage/BodyCipher.h"
#include "storage/Database.h"
#include "storage/Sql.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <optional>

using mini::Result;
using mini::net::Endpoint;
using mini::services::AuthService;
using mini::services::RegistrationResult;

namespace {

struct StubRequest
{
    QByteArray method;
    QString path;
    QByteArray body;
    QJsonObject json;
};

class StubHttpServer : public QObject
{
public:
    using Handler = std::function<QPair<int, QByteArray>(const StubRequest &)>;

    explicit StubHttpServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &StubHttpServer::accept);
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    quint16 port() const { return m_server.serverPort(); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    const QVector<StubRequest> &requests() const { return m_requests; }

    bool loginAvailable = true;
    int reissueStatus = 200;

    std::optional<StubRequest> lastRequestFor(const QString &path) const
    {
        for (auto it = m_requests.crbegin(); it != m_requests.crend(); ++it) {
            if (it->path == path) {
                return *it;
            }
        }
        return std::nullopt;
    }

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void read(QTcpSocket *socket)
    {
        m_buffers[socket] += socket->readAll();
        QByteArray &buffer = m_buffers[socket];
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        int length = 0;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                length = trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toInt();
            }
        }
        if (buffer.size() < headerEnd + 4 + length) {
            return;
        }

        StubRequest request;
        const QList<QByteArray> firstLine = buffer.left(buffer.indexOf("\r\n")).split(' ');
        request.method = firstLine.value(0);
        request.path = QString::fromLatin1(firstLine.value(1));
        request.body = buffer.mid(headerEnd + 4, length);
        request.json = QJsonDocument::fromJson(request.body).object();
        m_requests.append(request);
        m_buffers.remove(socket);

        const QPair<int, QByteArray> response =
            m_handler ? m_handler(request) : qMakePair(200, QByteArrayLiteral("{}"));
        const QByteArray reason = response.first == 201 ? "Created"
            : response.first == 200 ? "OK"
            : response.first == 401 ? "Unauthorized"
            : "Error";
        QByteArray out = "HTTP/1.1 " + QByteArray::number(response.first) + ' ' + reason
            + "\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(response.second.size()) + "\r\nConnection: close\r\n\r\n";
        out += response.second;
        socket->write(out);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    Handler m_handler;
    QVector<StubRequest> m_requests;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

QByteArray jsonBody(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray sessionBody(const QString &login, const QString &deviceId, int status = 200)
{
    Q_UNUSED(status)
    QJsonObject root;
    root.insert(QStringLiteral("account"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("u_1")},
                            {QStringLiteral("login"), login},
                            {QStringLiteral("display_name"), QStringLiteral("Alice")}});
    root.insert(QStringLiteral("device"),
                QJsonObject{{QStringLiteral("id"), deviceId},
                            {QStringLiteral("name"), QStringLiteral("test")}});
    root.insert(QStringLiteral("tokens"),
                QJsonObject{{QStringLiteral("access_token"), QStringLiteral("access")},
                            {QStringLiteral("refresh_token"), QStringLiteral("refresh")}});
    return jsonBody(root);
}

bool seedEncryptedDatabase(const QString &path, const QByteArray &kenc)
{
    const QByteArray dbKey = mini::crypto::Kdf::deriveDbKey(kenc).value();
    auto opened = mini::storage::Database::open(path, dbKey);
    if (opened.isError()) {
        return false;
    }
    std::unique_ptr<mini::storage::Database> database = std::move(opened.value());
    mini::storage::BodyCipher cipher(dbKey);
    const auto body = cipher.encrypt(QStringLiteral("hello"),
                                     mini::storage::messageAad(QStringLiteral("chat-1"),
                                                               QStringLiteral("msg-1")));
    const auto preview = cipher.encrypt(QStringLiteral("hi"),
                                        mini::storage::chatPreviewAad(QStringLiteral("chat-1")));
    if (body.isError() || preview.isError()) {
        return false;
    }
    const QString sql = QStringLiteral(
        "INSERT INTO chats(chat_id, last_message_preview) VALUES('chat-1', X'%1');"
        "INSERT INTO messages(client_msg_id, chat_id, body, state)"
        " VALUES('msg-1', 'chat-1', X'%2', 'sent');")
        .arg(QString::fromLatin1(preview.value().toHex()),
             QString::fromLatin1(body.value().toHex()));
    return database->exec(sql.toUtf8()).isOk();
}

QByteArray readMessageBody(const QString &path, const QByteArray &kenc)
{
    const QByteArray dbKey = mini::crypto::Kdf::deriveDbKey(kenc).value();
    auto opened = mini::storage::Database::open(path, dbKey);
    if (opened.isError()) {
        return {};
    }
    std::unique_ptr<mini::storage::Database> database = std::move(opened.value());
    mini::storage::detail::Statement select(
        database->handle(), "SELECT body FROM messages WHERE client_msg_id = 'msg-1'");
    if (!select.isOk() || select.step() != SQLITE_ROW) {
        return {};
    }
    return select.blob(0);
}

bool writeIdentityKeys(const QString &path, const QByteArray &kenc, const QString &deviceId)
{
    const auto state = mini::crypto::IdentityKeys::generate(1000);
    const auto wrap = mini::crypto::Kdf::deriveKeyWrap(kenc);
    if (state.isError() || wrap.isError()) {
        return false;
    }
    return mini::crypto::IdentityKeys::save(state.value(), path, wrap.value(), deviceId).isOk();
}

} // namespace

class TestAuthService : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void validatesLoginRules();
    void validatesDisplayNameRules();
    void validatesPasswordRules();
    void missingSignInFileFailsBeforeNetwork();
    void refusesPlainHttpWithoutPin();
    void unavailableLoginStopsBeforePrepare();
    void registrationAndLoginSpeakTheServerContract();
    void registrationMintsFreshDeviceIdEvenWhenOneIsPersisted();
    void reissueReencryptsDatabaseAndKeys();
    void reissueKeysFailureRollsBackDatabase();
    void reissueServerFailureRollsBackLocalKeys();
    void reissueFailureKeepsOldSignInAndDeleteClearsSession();

private:
    bool loginAndRegister(QString *signInPath, QString *errorOut);

    QTemporaryDir *m_temp = nullptr;
    StubHttpServer *m_server = nullptr;
    AuthService *m_service = nullptr;
    QByteArray m_saltFile = QByteArray(16, 'A');
    QByteArray m_saltPw = QByteArray(16, 'B');
};

void TestAuthService::init()
{
    m_temp = new QTemporaryDir();
    QVERIFY(m_temp->isValid());
    m_server = new StubHttpServer(this);
    QVERIFY(m_server->listen());
    m_service = new AuthService(Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"),
                                         m_server->port()),
                                this);
    m_service->setDeviceId(QStringLiteral("device-abc"));
    m_service->setSignInDirectory(QDir(m_temp->path()).filePath(QStringLiteral("files")));
    m_service->setAllowInsecureTransportForTesting(true);
    m_server->loginAvailable = true;
    m_server->reissueStatus = 200;

    m_server->setHandler([this](const StubRequest &request) -> QPair<int, QByteArray> {
        if (request.path.startsWith(QStringLiteral("/api/v1/auth/check_login"))) {
            return {200, jsonBody(QJsonObject{{QStringLiteral("login"), QStringLiteral("alice_01")},
                                              {QStringLiteral("available"), m_server->loginAvailable},
                                              {QStringLiteral("reason"),
                                               m_server->loginAvailable ? QStringLiteral("ok")
                                                                        : QStringLiteral("taken")}})};
        }
        if (request.path == QStringLiteral("/api/v1/auth/prepare")) {
            const QString purpose = request.json.value(QStringLiteral("purpose")).toString();
            QJsonObject body;
            body.insert(QStringLiteral("login"), request.json.value(QStringLiteral("login")));
            body.insert(QStringLiteral("purpose"), purpose);
            body.insert(QStringLiteral("salt_file"),
                        QString::fromLatin1(m_saltFile.toBase64(QByteArray::Base64UrlEncoding
                                                                | QByteArray::OmitTrailingEquals)));
            body.insert(QStringLiteral("salt_pw"),
                        QString::fromLatin1(m_saltPw.toBase64(QByteArray::Base64UrlEncoding
                                                              | QByteArray::OmitTrailingEquals)));
            body.insert(QStringLiteral("nonce"),
                        purpose == QStringLiteral("register") ? QStringLiteral("nonce-reg")
                                                              : QStringLiteral("nonce-login"));
            body.insert(QStringLiteral("captcha"),
                        QJsonObject{{QStringLiteral("provider"), QStringLiteral("builtin")},
                                    {QStringLiteral("required"), true}});
            return {200, jsonBody(body)};
        }
        if (request.path == QStringLiteral("/api/v1/auth/register")) {
            const QString device = request.json.value(QStringLiteral("device"))
                                       .toObject()
                                       .value(QStringLiteral("device_id"))
                                       .toString();
            return {201, sessionBody(request.json.value(QStringLiteral("login")).toString(),
                                     device)};
        }
        if (request.path == QStringLiteral("/api/v1/auth/login")) {            const auto registered = m_server->lastRequestFor(QStringLiteral("/api/v1/auth/register"));
            const QString expectedAuth = registered.has_value()
                ? registered->json.value(QStringLiteral("auth_key")).toString()
                : QString();
            const QString expectedPassword = registered.has_value()
                ? registered->json.value(QStringLiteral("password_key")).toString()
                : QString();
            if (request.json.value(QStringLiteral("signin_key")).toString() != expectedAuth
                || request.json.value(QStringLiteral("password_key")).toString() != expectedPassword
                || request.json.value(QStringLiteral("nonce")).toString() != QStringLiteral("nonce-login")) {
                return {401, jsonBody(QJsonObject{
                                 {QStringLiteral("error"),
                                  QJsonObject{{QStringLiteral("code"), QStringLiteral("invalid_credentials")}}}})};
            }
            const QString device = request.json.value(QStringLiteral("device"))
                                       .toObject()
                                       .value(QStringLiteral("device_id"))
                                       .toString();
            return {200, sessionBody(QStringLiteral("alice_01"), device)};
        }
        if (request.path == QStringLiteral("/api/v1/auth/reissue")) {
            if (m_server->reissueStatus != 200) {
                return {m_server->reissueStatus, QByteArray()};
            }
            return {200, jsonBody(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}})};
        }
        if (request.path == QStringLiteral("/api/v1/auth/delete_account")) {
            return {204, QByteArray()};
        }
        return {404, jsonBody(QJsonObject{
                         {QStringLiteral("error"),
                          QJsonObject{{QStringLiteral("code"), QStringLiteral("not_found")}}}})};
    });
}

void TestAuthService::cleanup()
{
    delete m_service;
    m_service = nullptr;
    delete m_server;
    m_server = nullptr;
    delete m_temp;
    m_temp = nullptr;
}

bool TestAuthService::loginAndRegister(QString *signInPath, QString *errorOut)
{
    bool registered = false;
    Result<RegistrationResult> registration = Result<RegistrationResult>::error(QString());
    m_service->registerAccount(QStringLiteral("alice_01"), QStringLiteral("Alice"),
                               QStringLiteral("Passw0rd!"), QStringLiteral("test-ticket"),
                               [&](Result<RegistrationResult> result) {
                                   registered = true;
                                   registration = result;
                               });
    QElapsedTimer timer;
    timer.start();
    while (!registered && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (!registered || registration.isError()) {
        *errorOut = registration.error();
        return false;
    }

    bool loggedIn = false;
    Result<mini::services::AuthSession> login =
        Result<mini::services::AuthSession>::error(QString());
    m_service->login(QStringLiteral("alice_01"), QStringLiteral("Passw0rd!"),
                     registration.value().filePath, QString(),
                     [&](Result<mini::services::AuthSession> result) {
                         loggedIn = true;
                         login = result;
                     });
    timer.restart();
    while (!loggedIn && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (!loggedIn || login.isError()) {
        *errorOut = login.error();
        return false;
    }
    *signInPath = registration.value().filePath;
    return true;
}

void TestAuthService::validatesLoginRules()
{
    QVERIFY(AuthService::validateLogin(QStringLiteral("alice_01")).isOk());
    QCOMPARE(AuthService::validateLogin(QStringLiteral("ab")).error(),
             QStringLiteral("login.invalid_format"));
    QCOMPARE(AuthService::validateLogin(QStringLiteral("1alice")).error(),
             QStringLiteral("login.invalid_format"));
    QCOMPARE(AuthService::validateLogin(QStringLiteral("Admin")).error(),
             QStringLiteral("login.reserved"));
    QCOMPARE(AuthService::validateLogin(QStringLiteral("bot_helper")).error(),
             QStringLiteral("login.reserved"));
}

void TestAuthService::validatesDisplayNameRules()
{
    QVERIFY(AuthService::validateDisplayName(QStringLiteral("  Кэрол  ")).isOk());
    QCOMPARE(AuthService::validateDisplayName(QStringLiteral("   ")).error(),
             QStringLiteral("name.invalid"));
    QCOMPARE(AuthService::validateDisplayName(QStringLiteral("bad\u0007name")).error(),
             QStringLiteral("name.invalid"));
}

void TestAuthService::validatesPasswordRules()
{
    QVERIFY(AuthService::validatePassword(QStringLiteral("Passw0rd!"), QStringLiteral("alice_01"),
                                          QStringLiteral("Alice")).isOk());
    QCOMPARE(AuthService::validatePassword(QStringLiteral("short1"), QStringLiteral("alice_01"),
                                           QString()).error(),
             QStringLiteral("password.length"));
    QCOMPARE(AuthService::validatePassword(QStringLiteral("onlyletters"), QStringLiteral("alice_01"),
                                           QString()).error(),
             QStringLiteral("password.composition"));
    QCOMPARE(AuthService::validatePassword(QStringLiteral("alice_01"), QStringLiteral("alice_01"),
                                           QString()).error(),
             QStringLiteral("password.equals_login"));
    QVERIFY(AuthService::passwordScore(QStringLiteral("Passw0rd!")) >= 3);
}

void TestAuthService::missingSignInFileFailsBeforeNetwork()
{
    AuthService service(Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"), 1), this);
    bool done = false;
    QString error;
    service.login(QStringLiteral("alice_01"), QStringLiteral("Passw0rd!"),
                  QDir(m_temp->path()).filePath(QStringLiteral("absent.in")), QString(),
                  [&](Result<mini::services::AuthSession> result) {
                      done = true;
                      error = result.error();
                  });
    QVERIFY(done);
    QCOMPARE(error, QStringLiteral("signin.missing"));
    QCOMPARE(m_server->requests().size(), 0);
}

void TestAuthService::registrationAndLoginSpeakTheServerContract()
{
    bool registered = false;
    Result<RegistrationResult> registration = Result<RegistrationResult>::error(QString());
    m_service->registerAccount(QStringLiteral("alice_01"), QStringLiteral("Alice"),
                               QStringLiteral("Passw0rd!"), QStringLiteral("test-ticket"),
                               [&](Result<RegistrationResult> result) {
                                   registered = true;
                                   registration = result;
                               });
    QTRY_VERIFY_WITH_TIMEOUT(registered, 30000);
    QVERIFY2(registration.isOk(), qPrintable(registration.error()));
    QVERIFY(QFileInfo::exists(registration.value().filePath));

    const auto registerRequest = m_server->lastRequestFor(QStringLiteral("/api/v1/auth/register"));
    QVERIFY(registerRequest.has_value());
    QCOMPARE(registerRequest->json.value(QStringLiteral("captcha_ticket")).toString(),
             QStringLiteral("test-ticket"));
    QVERIFY(registerRequest->json.contains(QStringLiteral("auth_key")));
    QVERIFY(registerRequest->json.contains(QStringLiteral("password_key")));
    QVERIFY(registerRequest->json.contains(QStringLiteral("nonce")));
    QVERIFY(!registerRequest->json.contains(QStringLiteral("k_auth")));
    const QString registeredDevice = registerRequest->json.value(QStringLiteral("device"))
                                         .toObject().value(QStringLiteral("device_id")).toString();
    QVERIFY(!registeredDevice.isEmpty());
    QVERIFY2(registeredDevice != QStringLiteral("device-abc"),
             "registration must not reuse the id persisted for another account");
    QCOMPARE(registeredDevice, registration.value().session.deviceId);
    const QString expectedAuth = registerRequest->json.value(QStringLiteral("auth_key")).toString();

    bool loggedIn = false;
    Result<mini::services::AuthSession> loginResult =
        Result<mini::services::AuthSession>::error(QString());
    m_service->login(QStringLiteral("alice_01"), QStringLiteral("Passw0rd!"),
                     registration.value().filePath, QString(),
                     [&](Result<mini::services::AuthSession> result) {
                         loggedIn = true;
                         loginResult = result;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(loggedIn, 30000);
    QVERIFY2(loginResult.isOk(), qPrintable(loginResult.error()));

    const auto loginRequest = m_server->lastRequestFor(QStringLiteral("/api/v1/auth/login"));
    QVERIFY(loginRequest.has_value());
    QCOMPARE(loginRequest->json.value(QStringLiteral("signin_key")).toString(), expectedAuth);
    QVERIFY(loginRequest->json.contains(QStringLiteral("password_key")));
    QVERIFY(m_service->hasSession());
    QCOMPARE(m_service->session().login, QStringLiteral("alice_01"));
}

void TestAuthService::refusesPlainHttpWithoutPin()
{
    AuthService service(Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"),
                                 m_server->port()),
                        this);
    bool done = false;
    QString error;
    service.checkLogin(QStringLiteral("alice_01"),
                       [&](Result<mini::services::LoginAvailability> result) {
                           done = true;
                           error = result.error();
                       });
    QVERIFY(done);
    QCOMPARE(error, QStringLiteral("tls_required"));
    QCOMPARE(m_server->requests().size(), 0);
}

void TestAuthService::unavailableLoginStopsBeforePrepare()
{
    m_server->loginAvailable = false;
    bool done = false;
    QString error;
    m_service->registerAccount(QStringLiteral("alice_01"), QStringLiteral("Alice"),
                               QStringLiteral("Passw0rd!"), QStringLiteral("test-ticket"),
                               [&](Result<RegistrationResult> result) {
                                   done = true;
                                   error = result.error();
                               });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QCOMPARE(error, QStringLiteral("login.taken"));
    QVERIFY(!m_server->lastRequestFor(QStringLiteral("/api/v1/auth/prepare")).has_value());
}

void TestAuthService::reissueReencryptsDatabaseAndKeys()
{
    QString path;
    QString error;
    QVERIFY2(loginAndRegister(&path, &error), qPrintable(error));
    const QByteArray oldKenc = m_service->fileKeys().kenc;
    QVERIFY(!oldKenc.isEmpty());

    const QString dataDir = QDir(m_temp->path()).filePath(QStringLiteral("data"));
    QVERIFY(QDir().mkpath(dataDir));
    const QString dbPath = QDir(dataDir).filePath(QStringLiteral("messages.db"));
    const QString keysPath = QDir(dataDir).filePath(QStringLiteral("keys.bin"));
    QVERIFY(seedEncryptedDatabase(dbPath, oldKenc));
    // Registration mints this installation's own device id; the identity
    // file must be written under the same id the session carries.
    const QString deviceId = m_service->session().deviceId;
    QVERIFY(!deviceId.isEmpty());
    QVERIFY(writeIdentityKeys(keysPath, oldKenc, deviceId));
    QVERIFY(mini::crypto::IdentityKeys::load(keysPath,
                                             mini::crypto::Kdf::deriveKeyWrap(oldKenc).value(),
                                             deviceId)
                .isOk());
    m_service->setLocalDataPaths(dbPath, keysPath);

    bool reissued = false;
    Result<mini::services::ReissueResult> result =
        Result<mini::services::ReissueResult>::error(QString());
    m_service->reissueSignIn([&](Result<mini::services::ReissueResult> value) {
        reissued = true;
        result = value;
    });
    QTRY_VERIFY_WITH_TIMEOUT(reissued, 60000);
    QVERIFY2(result.isOk(), qPrintable(result.error()));

    const QByteArray newKenc = m_service->fileKeys().kenc;
    QVERIFY(newKenc != oldKenc);

    const QByteArray body = readMessageBody(dbPath, newKenc);
    QVERIFY(!body.isEmpty());
    const QByteArray aad =
        mini::storage::messageAad(QStringLiteral("chat-1"), QStringLiteral("msg-1"));
    mini::storage::BodyCipher newCipher(
        mini::crypto::Kdf::deriveDbKey(newKenc).value());
    mini::storage::BodyCipher oldCipher(
        mini::crypto::Kdf::deriveDbKey(oldKenc).value());
    const auto plain = newCipher.decrypt(body, aad);
    QVERIFY(plain.isOk());
    QCOMPARE(plain.value(), QStringLiteral("hello"));
    QVERIFY(oldCipher.decrypt(body, aad).isError());

    const auto newWrap = mini::crypto::Kdf::deriveKeyWrap(newKenc);
    const auto oldWrap = mini::crypto::Kdf::deriveKeyWrap(oldKenc);
    QVERIFY(mini::crypto::IdentityKeys::load(keysPath, newWrap.value(), deviceId)
                .isOk());
    QVERIFY(mini::crypto::IdentityKeys::load(keysPath, oldWrap.value(), deviceId)
                .isError());
}

void TestAuthService::reissueKeysFailureRollsBackDatabase()
{
    QString path;
    QString error;
    QVERIFY2(loginAndRegister(&path, &error), qPrintable(error));
    const QByteArray oldKenc = m_service->fileKeys().kenc;

    const QString dataDir = QDir(m_temp->path()).filePath(QStringLiteral("data"));
    QVERIFY(QDir().mkpath(dataDir));
    const QString dbPath = QDir(dataDir).filePath(QStringLiteral("messages.db"));
    const QString keysPath = QDir(dataDir).filePath(QStringLiteral("keys.bin"));
    QVERIFY(seedEncryptedDatabase(dbPath, oldKenc));

    // keys.bin exists but is sealed with another wrap, so loading it with the
    // current key fails after the database has already been re-encrypted.
    const auto state = mini::crypto::IdentityKeys::generate(1000);
    QVERIFY(state.isOk());
    const QByteArray wrongWrap = mini::crypto::Kdf::deriveKeyWrap(QByteArray(32, 'Z')).value();
    QVERIFY(mini::crypto::IdentityKeys::save(state.value(), keysPath, wrongWrap,
                                             m_service->session().deviceId)
                .isOk());
    QFile keysFile(keysPath);
    QVERIFY(keysFile.open(QIODevice::ReadOnly));
    const QByteArray keysBefore = keysFile.readAll();
    keysFile.close();
    m_service->setLocalDataPaths(dbPath, keysPath);

    bool reissued = false;
    QString reissueError;
    m_service->reissueSignIn([&](Result<mini::services::ReissueResult> value) {
        reissued = true;
        reissueError = value.error();
    });
    QTRY_VERIFY_WITH_TIMEOUT(reissued, 60000);
    QCOMPARE(reissueError, QStringLiteral("rekey.keys_load"));
    QCOMPARE(m_service->fileKeys().kenc, oldKenc);

    const QByteArray body = readMessageBody(dbPath, oldKenc);
    QVERIFY(!body.isEmpty());
    mini::storage::BodyCipher oldCipher(
        mini::crypto::Kdf::deriveDbKey(oldKenc).value());
    QVERIFY(oldCipher.decrypt(body, mini::storage::messageAad(
                                       QStringLiteral("chat-1"),
                                       QStringLiteral("msg-1")))
                .isOk());

    QVERIFY(QFileInfo::exists(keysPath));
    QVERIFY(keysFile.open(QIODevice::ReadOnly));
    QCOMPARE(keysFile.readAll(), keysBefore);
    keysFile.close();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(mini::crypto::SignInKey::normalize(file.readAll()).isOk());
}

void TestAuthService::reissueServerFailureRollsBackLocalKeys()
{
    QString path;
    QString error;
    QVERIFY2(loginAndRegister(&path, &error), qPrintable(error));
    const QByteArray oldKenc = m_service->fileKeys().kenc;

    const QString dataDir = QDir(m_temp->path()).filePath(QStringLiteral("data"));
    QVERIFY(QDir().mkpath(dataDir));
    const QString dbPath = QDir(dataDir).filePath(QStringLiteral("messages.db"));
    const QString keysPath = QDir(dataDir).filePath(QStringLiteral("keys.bin"));
    QVERIFY(seedEncryptedDatabase(dbPath, oldKenc));
    QVERIFY(writeIdentityKeys(keysPath, oldKenc, m_service->session().deviceId));
    m_service->setLocalDataPaths(dbPath, keysPath);
    m_server->reissueStatus = 500;

    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray fileBefore = beforeFile.readAll();
    beforeFile.close();

    bool reissued = false;
    QString reissueError;
    m_service->reissueSignIn([&](Result<mini::services::ReissueResult> value) {
        reissued = true;
        reissueError = value.error();
    });
    QTRY_VERIFY_WITH_TIMEOUT(reissued, 60000);
    QVERIFY(!reissueError.isEmpty());
    QCOMPARE(m_service->fileKeys().kenc, oldKenc);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray fileAfter = file.readAll();
    QCOMPARE(fileAfter, fileBefore);
    QVERIFY(mini::crypto::SignInKey::normalize(fileAfter).isOk());

    const QByteArray body = readMessageBody(dbPath, oldKenc);
    QVERIFY(!body.isEmpty());
    mini::storage::BodyCipher oldCipher(
        mini::crypto::Kdf::deriveDbKey(oldKenc).value());
    QVERIFY(oldCipher.decrypt(body, mini::storage::messageAad(
                                       QStringLiteral("chat-1"),
                                       QStringLiteral("msg-1")))
                .isOk());
    const auto oldWrap = mini::crypto::Kdf::deriveKeyWrap(oldKenc);
    QVERIFY(mini::crypto::IdentityKeys::load(keysPath, oldWrap.value(),
                                             m_service->session().deviceId)
                .isOk());
}

void TestAuthService::reissueFailureKeepsOldSignInAndDeleteClearsSession()
{
    bool registered = false;
    Result<RegistrationResult> registration = Result<RegistrationResult>::error(QString());
    m_service->registerAccount(QStringLiteral("alice_01"), QStringLiteral("Alice"),
                               QStringLiteral("Passw0rd!"), QStringLiteral("test-ticket"),
                               [&](Result<RegistrationResult> result) {
                                   registered = true;
                                   registration = result;
                               });
    QTRY_VERIFY_WITH_TIMEOUT(registered, 30000);
    QVERIFY2(registration.isOk(), qPrintable(registration.error()));
    const QString oldPath = registration.value().filePath;

    bool loggedIn = false;
    Result<mini::services::AuthSession> loginResult =
        Result<mini::services::AuthSession>::error(QString());
    m_service->login(QStringLiteral("alice_01"), QStringLiteral("Passw0rd!"), oldPath, QString(),
                     [&](Result<mini::services::AuthSession> result) {
                         loggedIn = true;
                         loginResult = result;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(loggedIn, 30000);
    QVERIFY2(loginResult.isOk(), qPrintable(loginResult.error()));

    m_service->setDatabaseRekey([](const QByteArray &, const QByteArray &) {
        return Result<bool>::error(QStringLiteral("disk_full"));
    });
    bool reissued = false;
    QString reissueError;
    m_service->reissueSignIn([&](Result<mini::services::ReissueResult> result) {
        reissued = true;
        reissueError = result.error();
    });
    QTRY_VERIFY_WITH_TIMEOUT(reissued, 30000);
    QCOMPARE(reissueError, QStringLiteral("disk_full"));
    QVERIFY2(mini::crypto::SignInKey::normalize(
                 [&] {
                     QFile file(oldPath);
                     file.open(QIODevice::ReadOnly);
                     return file.readAll();
                 }())
                 .isOk(),
             "the old sign.in must keep working after a failed reissue");
    QVERIFY(!m_server->lastRequestFor(QStringLiteral("/api/v1/auth/reissue")).has_value());

    bool wrongDelete = false;
    QString wrongDeleteError;
    m_service->deleteAccount(QStringLiteral("someone_else"), QStringLiteral("Passw0rd!"), true,
                             [&](Result<bool> result) {
                                 wrongDelete = true;
                                 wrongDeleteError = result.error();
                             });
    QTRY_VERIFY_WITH_TIMEOUT(wrongDelete, 30000);
    QCOMPARE(wrongDeleteError, QStringLiteral("delete.login_mismatch"));
    QVERIFY(QFileInfo::exists(oldPath));

    bool deleted = false;
    bool deletedOk = false;
    m_service->deleteAccount(QStringLiteral("alice_01"), QStringLiteral("Passw0rd!"), true,
                             [&](Result<bool> result) {
                                 deleted = true;
                                 deletedOk = result.isOk();
                             });
    QTRY_VERIFY_WITH_TIMEOUT(deleted, 30000);
    QVERIFY(deletedOk);
    QVERIFY(!m_service->hasSession());
    QVERIFY(!QFileInfo::exists(oldPath));
}

QTEST_GUILESS_MAIN(TestAuthService)
#include "test_auth_service.moc"

void TestAuthService::registrationMintsFreshDeviceIdEvenWhenOneIsPersisted()
{
    // A second account on the same client arrives with the first account's
    // persisted installation id. Registration must generate a new device
    // binding instead of sending that id again.
    m_service->setDeviceId(QStringLiteral("device-from-first-account"));

    bool registered = false;
    Result<RegistrationResult> registration = Result<RegistrationResult>::error(QString());
    m_service->registerAccount(QStringLiteral("bob_01"), QStringLiteral("Bob"),
                               QStringLiteral("Passw0rd!"), QStringLiteral("test-ticket"),
                               [&](Result<RegistrationResult> result) {
                                   registered = true;
                                   registration = result;
                               });
    QTRY_VERIFY_WITH_TIMEOUT(registered, 30000);
    QVERIFY2(registration.isOk(), qPrintable(registration.error()));

    const auto request = m_server->lastRequestFor(QStringLiteral("/api/v1/auth/register"));
    QVERIFY(request.has_value());
    const QString sent = request->json.value(QStringLiteral("device"))
                             .toObject().value(QStringLiteral("device_id")).toString();
    QVERIFY(!sent.isEmpty());
    QVERIFY2(sent != QStringLiteral("device-from-first-account"),
             "the first account's device id leaked into the second registration");
    QCOMPARE(m_service->deviceId(), registration.value().session.deviceId);
}
