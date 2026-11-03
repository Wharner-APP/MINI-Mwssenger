#include "StubWsServer.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QByteArray headerValue(const QList<QByteArray> &lines, const QByteArray &name)
{
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        if (line.left(colon).trimmed().toLower() == name.toLower()) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

} // namespace

StubWsServer::StubWsServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &StubWsServer::onNewConnection);
}

bool StubWsServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 StubWsServer::port() const
{
    return m_server->serverPort();
}

QString StubWsServer::url() const
{
    return QStringLiteral("ws://127.0.0.1:%1/ws").arg(port());
}

void StubWsServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        socket->setParent(this);
        m_client = socket;
        m_buffers.insert(socket, QByteArray());
        m_handshaken.insert(socket, false);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            m_handshaken.remove(socket);
            if (m_client == socket) {
                m_client = nullptr;
            }
            socket->deleteLater();
        });
    }
}

void StubWsServer::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    if (!m_handshaken.value(socket)) {
        processHandshake(socket);
    }
    if (m_handshaken.value(socket)) {
        processFrames(socket);
    }
}

void StubWsServer::processHandshake(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    const int end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }

    const QByteArray headerBlock = buffer.left(end);
    buffer.remove(0, end + 4);

    const QList<QByteArray> lines = headerBlock.split('\n');
    const QByteArray key = headerValue(lines, "Sec-WebSocket-Key");
    if (key.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    const QByteArray accept =
        QCryptographicHash::hash(key + QByteArray(kGuid), QCryptographicHash::Sha1)
            .toBase64();

    QByteArray response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept + "\r\n";
    response += "\r\n";
    socket->write(response);

    m_handshaken[socket] = true;
    emit clientConnected();
}

void StubWsServer::processFrames(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    while (buffer.size() >= 2) {
        const quint8 first = static_cast<quint8>(buffer.at(0));
        const quint8 second = static_cast<quint8>(buffer.at(1));
        const bool fin = (first & 0x80) != 0;
        Q_UNUSED(fin);
        const int opcode = first & 0x0F;
        const bool masked = (second & 0x80) != 0;
        quint64 length = second & 0x7F;
        int offset = 2;

        if (length == 126) {
            if (buffer.size() < 4) {
                return;
            }
            length = (static_cast<quint8>(buffer.at(2)) << 8)
                | static_cast<quint8>(buffer.at(3));
            offset = 4;
        } else if (length == 127) {
            if (buffer.size() < 10) {
                return;
            }
            length = 0;
            for (int index = 0; index < 8; ++index) {
                length = (length << 8) | static_cast<quint8>(buffer.at(2 + index));
            }
            offset = 10;
        }

        QByteArray mask;
        if (masked) {
            if (buffer.size() < offset + 4) {
                return;
            }
            mask = buffer.mid(offset, 4);
            offset += 4;
        }
        if (static_cast<quint64>(buffer.size()) < static_cast<quint64>(offset) + length) {
            return;
        }

        QByteArray payload = buffer.mid(offset, static_cast<int>(length));
        buffer.remove(0, offset + static_cast<int>(length));
        if (masked) {
            for (int index = 0; index < payload.size(); ++index) {
                payload[index] =
                    static_cast<char>(payload.at(index) ^ mask.at(index % 4));
            }
        }

        if (opcode == 0x1) {
            emit textReceived(QString::fromUtf8(payload));
        } else if (opcode == 0x9) {
            sendFrame(socket, 0xA, payload);
        } else if (opcode == 0xA) {
            emit pongReceived();
        } else if (opcode == 0x8) {
            int code = 1000;
            if (payload.size() >= 2) {
                code = (static_cast<quint8>(payload.at(0)) << 8)
                    | static_cast<quint8>(payload.at(1));
            }
            QByteArray echo;
            echo.append(static_cast<char>((code >> 8) & 0xFF));
            echo.append(static_cast<char>(code & 0xFF));
            sendFrame(socket, 0x8, echo);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }
    }
}

void StubWsServer::sendFrame(QTcpSocket *socket, int opcode, const QByteArray &payload)
{
    if (!socket) {
        return;
    }
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));
    const int length = payload.size();
    if (length < 126) {
        frame.append(static_cast<char>(length));
    } else if (length <= 0xFFFF) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((length >> 8) & 0xFF));
        frame.append(static_cast<char>(length & 0xFF));
    } else {
        frame.append(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((static_cast<quint64>(length) >> shift) & 0xFF));
        }
    }
    frame.append(payload);
    socket->write(frame);
}

void StubWsServer::sendFrameToClient(int opcode, const QByteArray &payload)
{
    sendFrame(m_client, opcode, payload);
}

void StubWsServer::sendText(const QString &text)
{
    sendFrameToClient(0x1, text.toUtf8());
}

void StubWsServer::sendPing(const QByteArray &payload)
{
    sendFrameToClient(0x9, payload);
}

void StubWsServer::sendClose(int code, const QString &reason)
{
    QByteArray payload;
    payload.append(static_cast<char>((code >> 8) & 0xFF));
    payload.append(static_cast<char>(code & 0xFF));
    payload += reason.toUtf8();
    sendFrameToClient(0x8, payload);
}
