#include "StubHttpServer.h"

#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {

QByteArray statusText(int status)
{
    switch (status) {
    case 200:
        return QByteArrayLiteral("OK");
    case 204:
        return QByteArrayLiteral("No Content");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 401:
        return QByteArrayLiteral("Unauthorized");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 429:
        return QByteArrayLiteral("Too Many Requests");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    default:
        return QByteArrayLiteral("Status");
    }
}

} // namespace

StubHttpServer::StubHttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &StubHttpServer::onNewConnection);
}

bool StubHttpServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 StubHttpServer::port() const
{
    return m_server->serverPort();
}

QString StubHttpServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void StubHttpServer::setHandler(Handler handler)
{
    m_handler = std::move(handler);
}

void StubHttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        socket->setParent(this);
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void StubHttpServer::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray headerBlock = buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');

    Request request;
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() < 2) {
        socket->disconnectFromHost();
        return;
    }
    request.method = requestLine.at(0);
    const QString target = QString::fromUtf8(requestLine.at(1));
    const int queryAt = target.indexOf(QLatin1Char('?'));
    request.path = queryAt >= 0 ? target.left(queryAt) : target;
    request.query = QUrlQuery(queryAt >= 0 ? target.mid(queryAt + 1) : QString());

    for (int index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        request.headers.insert(line.left(colon).trimmed().toLower(),
                               line.mid(colon + 1).trimmed());
    }

    const int contentLength = request.headers.value("content-length").toInt();
    if (buffer.size() < headerEnd + 4 + contentLength) {
        return;
    }
    request.body = buffer.mid(headerEnd + 4, contentLength);
    buffer.clear();

    m_requests.append(request);
    respond(socket, request);
}

void StubHttpServer::respond(QTcpSocket *socket, const Request &request)
{
    Response response;
    if (m_handler) {
        response = m_handler(request);
    } else {
        response.status = 404;
        response.body = QByteArrayLiteral("{\"error\":{\"code\":\"not_found\"}}");
    }

    QByteArray bytes;
    bytes += "HTTP/1.1 " + QByteArray::number(response.status) + " "
        + statusText(response.status) + "\r\n";
    bytes += "Content-Type: " + response.contentType + "\r\n";
    bytes += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    bytes += "Connection: close\r\n";
    for (auto it = response.headers.cbegin(); it != response.headers.cend(); ++it) {
        bytes += it.key() + ": " + it.value() + "\r\n";
    }
    bytes += "\r\n";
    bytes += response.body;

    auto send = [socket = QPointer<QTcpSocket>(socket), bytes]() {
        if (!socket) {
            return;
        }
        socket->write(bytes);
        socket->flush();
        socket->disconnectFromHost();
    };
    if (response.delayMs > 0) {
        QTimer::singleShot(response.delayMs, this, send);
        return;
    }
    send();
}
