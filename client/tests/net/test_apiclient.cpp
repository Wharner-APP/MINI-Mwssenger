#include "net/ApiClient.h"
#include "StubHttpServer.h"

#include <QSignalSpy>
#include <QtTest>

#include <optional>

using namespace mini::net;

class TestApiClient : public QObject
{
    Q_OBJECT

private slots:
    void prepareReturnsTypedFields();
    void errorCodeIsTypedAndNotJson();
    void rateLimitCarriesRetryAfter();
    void requestsCarryBearerToken();
    void pullMailParsesEnvelope();
    void protocolVersionMismatchIsReported();
    void networkFailureIsTyped();
};

namespace {

StubHttpServer::Response jsonResponse(const QByteArray &body, int status = 200)
{
    StubHttpServer::Response response;
    response.status = status;
    response.body = body;
    return response;
}

Endpoint endpointFor(const StubHttpServer &server)
{
    return Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"), server.port());
}

} // namespace

void TestApiClient::prepareReturnsTypedFields()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &request) {
        if (request.method != "POST" || request.path != QStringLiteral("/api/v1/auth/prepare")) {
            return jsonResponse("{}", 404);
        }
        return jsonResponse(QByteArrayLiteral(
            "{\"login\":\"alice\",\"salt_file\":\"c2FsdA==\",\"salt_pw\":\"cHc=\","
            "\"kdf\":{\"algo\":\"argon2id\",\"m\":65536,\"t\":3,\"p\":1,\"len\":32}}"));
    });

    ApiClient client(endpointFor(server));
    bool done = false;
    std::optional<ApiResult<AuthPrepare>> result;
    client.prepare(QStringLiteral("alice"), [&](ApiResult<AuthPrepare> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isOk());
    QCOMPARE(result->value().login, QStringLiteral("alice"));
    QCOMPARE(result->value().saltFile, QByteArrayLiteral("salt"));
    QCOMPARE(result->value().saltPw, QByteArrayLiteral("pw"));
    QCOMPARE(result->value().memoryKiB, 65536);

    QCOMPARE(server.requests().size(), 1);
    const StubHttpServer::Request &request = server.requests().first();
    QVERIFY(request.body.contains("\"login\":\"alice\""));
}

void TestApiClient::errorCodeIsTypedAndNotJson()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &) {
        return jsonResponse(QByteArrayLiteral(
            "{\"error\":{\"code\":\"invalid_credentials\",\"message\":\"bad\","
            "\"request_id\":\"req-1\"}}"), 401);
    });

    ApiClient client(endpointFor(server));
    QSignalSpy unauthorized(&client, &ApiClient::authenticationRequired);
    bool done = false;
    std::optional<ApiResult<AuthPrepare>> result;
    client.prepare(QStringLiteral("alice"), [&](ApiResult<AuthPrepare> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isError());
    QVERIFY(result->error().kind == ApiErrorKind::Unauthorized);
    QCOMPARE(result->error().code, QStringLiteral("invalid_credentials"));
    QCOMPARE(result->error().requestId, QStringLiteral("req-1"));
    QVERIFY(!result->error().isRetryable());
    QCOMPARE(unauthorized.count(), 1);
}

void TestApiClient::rateLimitCarriesRetryAfter()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &) {
        return jsonResponse(QByteArrayLiteral(
            "{\"error\":{\"code\":\"rate_limited\",\"message\":\"slow down\","
            "\"retry_after_ms\":5000}}"), 429);
    });

    ApiClient client(endpointFor(server));
    bool done = false;
    std::optional<ApiResult<LoginAvailability>> result;
    client.checkLogin(QStringLiteral("alice"), [&](ApiResult<LoginAvailability> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isError());
    QVERIFY(result->error().kind == ApiErrorKind::RateLimited);
    QCOMPARE(result->error().retryAfterMs, qint64(5000));
    QVERIFY(result->error().isRetryable());
}

void TestApiClient::requestsCarryBearerToken()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &request) {
        if (request.headers.value("authorization") != "Bearer token-123") {
            return jsonResponse("{}", 401);
        }
        return jsonResponse(QByteArrayLiteral(
            "{\"user\":{\"user_id\":\"u_1\",\"login\":\"alice\","
            "\"display_name\":\"Alice\",\"bio\":\"hi\",\"online\":true,"
            "\"last_active_at\":123}}"));
    });

    ApiClient client(endpointFor(server));
    client.setAccessToken(QStringLiteral("token-123"));
    bool done = false;
    std::optional<ApiResult<AccountProfile>> result;
    client.getMe([&](ApiResult<AccountProfile> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isOk());
    QCOMPARE(result->value().userId, QStringLiteral("u_1"));
    QCOMPARE(result->value().displayName, QStringLiteral("Alice"));
    QVERIFY(result->value().online);
}

void TestApiClient::pullMailParsesEnvelope()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &request) {
        if (request.path != QStringLiteral("/api/v1/mail/pull")
            || request.query.queryItemValue(QStringLiteral("device")) != QStringLiteral("dev1")) {
            return jsonResponse("{}", 400);
        }
        return jsonResponse(QByteArrayLiteral(
            "{\"items\":[{\"mail_id\":\"m1\",\"received_at\":100,\"envelope\":{"
            "\"kind\":\"text\",\"client_msg_id\":\"c1\",\"chat_id\":\"ch1\","
            "\"ciphertext_b64\":\"AAAA\",\"header\":{\"n\":\"x\"}}}],"
            "\"cursor\":\"next\",\"has_more\":true}"));
    });

    ApiClient client(endpointFor(server));
    bool done = false;
    std::optional<ApiResult<MailBatch>> result;
    client.pullMail(QStringLiteral("dev1"), QString(), [&](ApiResult<MailBatch> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isOk());
    QCOMPARE(result->value().items.size(), 1);
    QCOMPARE(result->value().items.first().mailId, QStringLiteral("m1"));
    QCOMPARE(result->value().items.first().envelope.clientMsgId, QStringLiteral("c1"));
    QCOMPARE(result->value().items.first().envelope.ciphertext, QByteArray(3, '\0'));
    QCOMPARE(result->value().cursor, QStringLiteral("next"));
    QVERIFY(result->value().hasMore);
}

void TestApiClient::protocolVersionMismatchIsReported()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &) {
        return jsonResponse(QByteArrayLiteral("{\"protocol_version\":2}"));
    });

    ApiClient client(endpointFor(server));
    QSignalSpy mismatch(&client, &ApiClient::protocolVersionMismatch);
    bool done = false;
    std::optional<ApiResult<AuthPrepare>> result;
    client.prepare(QStringLiteral("alice"), [&](ApiResult<AuthPrepare> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isError());
    QVERIFY(result->error().kind == ApiErrorKind::Protocol);
    QCOMPARE(mismatch.count(), 1);
}

void TestApiClient::networkFailureIsTyped()
{
    const Endpoint endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"), 1);
    ApiClient client(endpoint);
    client.setTransferTimeoutMs(1000);

    bool done = false;
    std::optional<ApiResult<AuthPrepare>> result;
    client.prepare(QStringLiteral("alice"), [&](ApiResult<AuthPrepare> value) {
        result = value;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(result->isError());
    QVERIFY(result->error().isRetryable());
    QVERIFY(result->error().kind == ApiErrorKind::Network
            || result->error().kind == ApiErrorKind::Timeout);
}

QTEST_MAIN(TestApiClient)
#include "test_apiclient.moc"
