#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

// Minimal RFC 6455 server for tests (text frames, ping/pong, close).
class StubWsServer : public QObject
{
    Q_OBJECT

public:
    explicit StubWsServer(QObject *parent = nullptr);

    bool listen();
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] QString url() const;

    void sendText(const QString &text);
    void sendPing(const QByteArray &payload = QByteArray());
    void sendClose(int code, const QString &reason = QString());

signals:
    void clientConnected();
    void textReceived(const QString &text);
    void pongReceived();

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void processHandshake(QTcpSocket *socket);
    void processFrames(QTcpSocket *socket);
    void sendFrame(QTcpSocket *socket, int opcode, const QByteArray &payload);
    void sendFrameToClient(int opcode, const QByteArray &payload);

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QHash<QTcpSocket *, bool> m_handshaken;
};
