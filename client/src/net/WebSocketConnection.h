#pragma once

#include <QByteArray>
#include <QObject>
#include <QUrl>

class QTcpSocket;
class QTimer;

namespace mini::net {

class TlsPin;

// Minimal RFC 6455 client transport over QTcpSocket/QSslSocket.
// Qt WebSockets is not installed in this Qt build, so framing lives here;
// TLS and certificate pinning still use Qt's SSL stack.
class WebSocketConnection : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Connecting,
        Handshaking,
        Open,
        Closing,
        Closed
    };
    Q_ENUM(State)

    explicit WebSocketConnection(TlsPin *pin = nullptr, QObject *parent = nullptr);
    ~WebSocketConnection() override;

    void open(const QUrl &url);
    void close(int code = 1000, const QString &reason = QString());
    void sendText(const QString &text);
    void sendPing(const QByteArray &payload = QByteArray());

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QUrl url() const { return m_url; }

signals:
    void opened();
    void textMessageReceived(const QString &text);
    void pongReceived();
    void closed(int code, const QString &reason);
    void transportError(const QString &reason, bool tlsMismatch);

private:
    void beginHandshake();
    void onReadyRead();
    void onDisconnected();
    void processHandshake();
    void processFrames();
    void processFrame(bool fin, int opcode, const QByteArray &payload);
    void sendFrame(int opcode, const QByteArray &payload);
    void sendClose(int code, const QString &reason);
    void fail(const QString &reason, bool tlsMismatch = false);
    void stopTimers();

    TlsPin *m_pin = nullptr;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_handshakeTimer = nullptr;
    QUrl m_url;
    State m_state = State::Idle;
    QByteArray m_buffer;
    QByteArray m_fragment;
    int m_fragmentOpcode = 0;
    QByteArray m_expectedAccept;
    int m_closeCode = 1000;
    QString m_closeReason;
    bool m_closedEmitted = false;
    bool m_pinApproved = false;
};

} // namespace mini::net
