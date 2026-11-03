#include "net/ApiClient.h"
#include "net/TlsPin.h"
#include "net/WsClient.h"
#include "StubHttpServer.h"
#include "StubWsServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace mini::net;

class TestWsClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void receivesAllFrameTypes();
    void answersServerPing();
    void ticketRequestsAreAuthenticated();
    void unauthorizedTicketIsNotRetried();
    void serverCloseSchedulesReconnect();
};

namespace {

Endpoint endpointFor(quint16 port)
{
    return Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"), port);
}

StubHttpServer::Response ticketResponse(const QString &ticket)
{
    StubHttpServer::Response response;
    response.body = QStringLiteral("{\"ticket\":\"%1\",\"expires_in\":300}")
                        .arg(ticket)
                        .toUtf8();
    return response;
}

} // namespace

void TestWsClient::initTestCase()
{
    qRegisterMetaType<Envelope>("mini::net::Envelope");
}

void TestWsClient::receivesAllFrameTypes()
{
    StubHttpServer http;
    QVERIFY(http.listen());
    http.setHandler([](const StubHttpServer::Request &request) {
        if (request.path != QStringLiteral("/api/v1/ws/ticket")) {
            return StubHttpServer::Response{404, {}, {}, {}};
        }
        return ticketResponse(QStringLiteral("ticket-1"));
    });

    StubWsServer wsServer;
    QVERIFY(wsServer.listen());

    ApiClient api(endpointFor(http.port()));
    TlsPin pin;
    WsClient ws(&api, &pin);

    QSignalSpy connected(&ws, &WsClient::connected);
    QSignalSpy hello(&ws, &WsClient::helloReceived);
    QSignalSpy message(&ws, &WsClient::messageReceived);
    QSignalSpy ack(&ws, &WsClient::ackReceived);
    QSignalSpy delivery(&ws, &WsClient::delivery);
    QSignalSpy read(&ws, &WsClient::read);
    QSignalSpy presence(&ws, &WsClient::presence);
    QSignalSpy typing(&ws, &WsClient::typing);
    QSignalSpy serverError(&ws, &WsClient::serverError);

    ws.start(endpointFor(wsServer.port()), QStringLiteral("token-1"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
    QVERIFY(ws.isConnected());

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"hello\",\"ts\":1,\"data\":{\"protocol_version\":1,"
        "\"server_time\":10}}"));
    QTRY_COMPARE_WITH_TIMEOUT(hello.count(), 1, 3000);
    QCOMPARE(hello.at(0).at(0).toString(), QStringLiteral("1"));
    QCOMPARE(hello.at(0).at(1).toLongLong(), qint64(10));

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"message\",\"ts\":2,\"data\":{\"kind\":\"text\","
        "\"client_msg_id\":\"c1\",\"chat_id\":\"ch1\",\"ciphertext_b64\":\"AAAA\","
        "\"header\":{\"n\":\"x\"}}}"));
    QTRY_COMPARE_WITH_TIMEOUT(message.count(), 1, 3000);
    const Envelope envelope = qvariant_cast<Envelope>(message.at(0).at(0));
    QCOMPARE(envelope.clientMsgId, QStringLiteral("c1"));
    QCOMPARE(envelope.chatId, QStringLiteral("ch1"));
    QCOMPARE(envelope.ciphertext, QByteArray(3, '\0'));

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"ack\",\"ts\":3,\"data\":{\"client_msg_id\":\"c1\","
        "\"chat_id\":\"ch1\"}}"));
    QTRY_COMPARE_WITH_TIMEOUT(ack.count(), 1, 3000);

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"delivery\",\"ts\":4,\"data\":{\"client_msg_id\":\"c1\","
        "\"chat_id\":\"ch1\",\"at\":44}}"));
    QTRY_COMPARE_WITH_TIMEOUT(delivery.count(), 1, 3000);
    QCOMPARE(delivery.at(0).at(2).toLongLong(), qint64(44));

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"read\",\"ts\":5,\"data\":{\"chat_id\":\"ch1\","
        "\"client_msg_ids\":[\"c1\",\"c2\"]}}"));
    QTRY_COMPARE_WITH_TIMEOUT(read.count(), 1, 3000);

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"presence\",\"ts\":6,\"data\":{\"user_id\":\"u1\","
        "\"online\":true,\"last_active_at\":99}}"));
    QTRY_COMPARE_WITH_TIMEOUT(presence.count(), 1, 3000);

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"typing\",\"ts\":7,\"data\":{\"chat_id\":\"ch1\","
        "\"user_id\":\"u1\",\"active\":true}}"));
    QTRY_COMPARE_WITH_TIMEOUT(typing.count(), 1, 3000);

    wsServer.sendText(QStringLiteral(
        "{\"v\":1,\"type\":\"error\",\"ts\":8,\"data\":{\"code\":\"chat_locked\","
        "\"message\":\"locked\"}}"));
    QTRY_COMPARE_WITH_TIMEOUT(serverError.count(), 1, 3000);
    QCOMPARE(serverError.at(0).at(0).toString(), QStringLiteral("chat_locked"));

    ws.stop();
}

void TestWsClient::answersServerPing()
{
    StubHttpServer http;
    QVERIFY(http.listen());
    http.setHandler([](const StubHttpServer::Request &) {
        return ticketResponse(QStringLiteral("ticket-2"));
    });

    StubWsServer wsServer;
    QVERIFY(wsServer.listen());

    ApiClient api(endpointFor(http.port()));
    TlsPin pin;
    WsClient ws(&api, &pin);
    ws.setPingInterval(200);
    ws.setPongTimeout(1000);

    QSignalSpy connected(&ws, &WsClient::connected);
    QSignalSpy serverPong(&wsServer, &StubWsServer::pongReceived);
    QSignalSpy clientPong(&ws, &WsClient::pongReceived);
    ws.start(endpointFor(wsServer.port()), QStringLiteral("token-1"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);

    wsServer.sendPing("probe");
    QTRY_COMPARE_WITH_TIMEOUT(serverPong.count(), 1, 3000);

    // The client's own ping is answered by the stub, so the session survives
    // and pongReceived() proves the reply actually arrived.
    QTRY_VERIFY_WITH_TIMEOUT(clientPong.count() >= 1, 3000);
    QVERIFY(ws.isConnected());
    ws.stop();
}

void TestWsClient::ticketRequestsAreAuthenticated()
{
    StubHttpServer http;
    QVERIFY(http.listen());
    http.setHandler([](const StubHttpServer::Request &request) {
        if (request.headers.value("authorization") != "Bearer token-1"
            || request.path != QStringLiteral("/api/v1/ws/ticket")) {
            return StubHttpServer::Response{401, {}, {}, {}};
        }
        return ticketResponse(QStringLiteral("ticket-3"));
    });

    StubWsServer wsServer;
    QVERIFY(wsServer.listen());

    ApiClient api(endpointFor(http.port()));
    TlsPin pin;
    WsClient ws(&api, &pin);
    QSignalSpy connected(&ws, &WsClient::connected);
    ws.start(endpointFor(wsServer.port()), QStringLiteral("token-1"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
    ws.stop();
}

void TestWsClient::unauthorizedTicketIsNotRetried()
{
    StubHttpServer http;
    QVERIFY(http.listen());
    http.setHandler([](const StubHttpServer::Request &) {
        StubHttpServer::Response response;
        response.status = 401;
        response.body = QByteArrayLiteral(
            "{\"error\":{\"code\":\"unauthorized\",\"message\":\"expired\"}}");
        return response;
    });

    ApiClient api(endpointFor(http.port()));
    TlsPin pin;
    WsClient ws(&api, &pin);
    QSignalSpy failed(&ws, &WsClient::authenticationFailed);
    QSignalSpy scheduled(&ws, &WsClient::reconnectScheduled);

    ws.start(endpointFor(http.port()), QStringLiteral("bad-token"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QTest::qWait(300);
    QCOMPARE(scheduled.count(), 0);
    QVERIFY(!ws.isConnected());
}

void TestWsClient::serverCloseSchedulesReconnect()
{
    StubHttpServer http;
    QVERIFY(http.listen());
    http.setHandler([](const StubHttpServer::Request &) {
        return ticketResponse(QStringLiteral("ticket-4"));
    });

    StubWsServer wsServer;
    QVERIFY(wsServer.listen());

    ApiClient api(endpointFor(http.port()));
    TlsPin pin;
    WsClient ws(&api, &pin);
    QSignalSpy connected(&ws, &WsClient::connected);
    QSignalSpy disconnected(&ws, &WsClient::disconnected);
    QSignalSpy scheduled(&ws, &WsClient::reconnectScheduled);

    ws.start(endpointFor(wsServer.port()), QStringLiteral("token-1"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);

    wsServer.sendClose(1001, QStringLiteral("going away"));
    QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(scheduled.count(), 1, 3000);
    QVERIFY(scheduled.at(0).at(0).toInt() >= 400);
    ws.stop();
}

QTEST_MAIN(TestWsClient)
#include "test_wsclient.moc"
