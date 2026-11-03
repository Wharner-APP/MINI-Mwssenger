#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>

class QTcpServer;
class QTcpSocket;

// Minimal local HTTP/1.1 server for tests. No internet is involved.
class StubHttpServer : public QObject
{
    Q_OBJECT

public:
    struct Request {
        QByteArray method;
        QString path;
        QUrlQuery query;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray contentType = QByteArrayLiteral("application/json");
        QByteArray body;
        QHash<QByteArray, QByteArray> headers;
        // Write the response after this many milliseconds instead of at once.
        // Used to test callers that may be destroyed while a request is in
        // flight.
        int delayMs = 0;
    };

    using Handler = std::function<Response(const Request &)>;

    explicit StubHttpServer(QObject *parent = nullptr);

    bool listen();
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] QString baseUrl() const;

    void setHandler(Handler handler);
    [[nodiscard]] const QList<Request> &requests() const { return m_requests; }
    void clearRequests() { m_requests.clear(); }

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket, const Request &request);

    QTcpServer *m_server = nullptr;
    Handler m_handler;
    QList<Request> m_requests;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

Q_DECLARE_METATYPE(StubHttpServer::Request)
