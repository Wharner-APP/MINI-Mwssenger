#include "net/WebSocketConnection.h"

#include "net/TlsPin.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

#include <cstring>

namespace mini::net {

namespace {

constexpr quint64 kMaxFrameBytes = 1024 * 1024;
constexpr int kConnectTimeoutMs = 8000;
constexpr int kHandshakeTimeoutMs = 15000;
const char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, 0);
    for (int index = 0; index < count; index += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        const int chunk = qMin(4, count - index);
        std::memcpy(bytes.data() + index, &value, static_cast<size_t>(chunk));
    }
    return bytes;
}

QByteArray headerValue(const QList<QByteArray> &lines, const QByteArray &name)
{
    const QByteArray lowerName = name.toLower();
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        if (line.left(colon).trimmed().toLower() == lowerName) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

} // namespace

WebSocketConnection::WebSocketConnection(TlsPin *pin, QObject *parent)
    : QObject(parent)
    , m_pin(pin)
    , m_connectTimer(new QTimer(this))
    , m_handshakeTimer(new QTimer(this))
{
    m_connectTimer->setSingleShot(true);
    m_handshakeTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this,
            [this]() { fail(QStringLiteral("connect_timeout")); });
    connect(m_handshakeTimer, &QTimer::timeout, this,
            [this]() { fail(QStringLiteral("handshake_timeout")); });
}

WebSocketConnection::~WebSocketConnection()
{
    if (m_socket) {
        m_socket->disconnect(this);
    }
}

void WebSocketConnection::open(const QUrl &url)
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    stopTimers();

    m_url = url;
    m_buffer.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
    m_expectedAccept.clear();
    m_closeCode = 1000;
    m_closeReason.clear();
    m_closedEmitted = false;
    m_pinApproved = false;
    m_state = State::Connecting;

    const bool secure = url.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0;
    QTcpSocket *socket = secure ? new QSslSocket(this) : new QTcpSocket(this);
    m_socket = socket;

    connect(socket, &QTcpSocket::readyRead, this, &WebSocketConnection::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &WebSocketConnection::onDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                if (m_state != State::Closed && m_state != State::Closing) {
                    fail(m_socket ? m_socket->errorString()
                                  : QStringLiteral("socket_error"));
                }
            });

    m_connectTimer->start(kConnectTimeoutMs);

    if (secure) {
        auto *ssl = qobject_cast<QSslSocket *>(socket);
        QSslConfiguration configuration = ssl->sslConfiguration();
        configuration.setProtocol(QSsl::TlsV1_3);
        ssl->setSslConfiguration(configuration);
        connect(ssl, &QSslSocket::sslErrors, this,
                [this, ssl](const QList<QSslError> &) {
                    const QSslCertificate certificate = ssl->peerCertificate();
                    const TlsPin::Outcome outcome =
                        m_pin ? m_pin->check(certificate)
                              : TlsPin::Outcome::NotConfigured;
                    if (m_pin && m_pin->shouldIgnore(certificate)) {
                        m_pinApproved = true;
                        ssl->ignoreSslErrors();
                        return;
                    }
                    fail(outcome == TlsPin::Outcome::Mismatch
                             ? QStringLiteral("certificate_fingerprint_mismatch")
                             : QStringLiteral("certificate_pin_required"),
                         true);
                });
        connect(ssl, &QSslSocket::encrypted, this, &WebSocketConnection::beginHandshake);
        ssl->connectToHostEncrypted(url.host(), static_cast<quint16>(url.port(443)));
    } else {
        connect(socket, &QTcpSocket::connected, this,
                &WebSocketConnection::beginHandshake);
        socket->connectToHost(url.host(), static_cast<quint16>(url.port(80)));
    }
}

void WebSocketConnection::beginHandshake()
{
    if (m_state != State::Connecting || !m_socket) {
        return;
    }

    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket)) {
        // A CA-valid certificate does not raise sslErrors, so the pin is
        // checked here as well. A decision already made in sslErrors is not
        // repeated (acceptMismatchOnce must stay one-shot).
        if (!m_pinApproved) {
            const QSslCertificate certificate = ssl->peerCertificate();
            const TlsPin::Outcome outcome =
                m_pin ? m_pin->check(certificate) : TlsPin::Outcome::NotConfigured;
            if (outcome == TlsPin::Outcome::Match) {
                m_pinApproved = true;
            } else if (outcome == TlsPin::Outcome::Mismatch) {
                if (m_pin && m_pin->shouldIgnore(certificate)) {
                    m_pinApproved = true;
                } else {
                    fail(QStringLiteral("certificate_fingerprint_mismatch"), true);
                    return;
                }
            } else {
                if (m_pin) {
                    // Emits pinUnavailable() as a warning before refusing WSS.
                    m_pin->shouldIgnore(certificate);
                }
                fail(QStringLiteral("certificate_pin_required"), true);
                return;
            }
        }
    }

    m_state = State::Handshaking;
    m_connectTimer->stop();
    m_handshakeTimer->start(kHandshakeTimeoutMs);

    const QByteArray key = randomBytes(16).toBase64();
    m_expectedAccept = QCryptographicHash::hash(key + QByteArray(kWebSocketGuid),
                                                QCryptographicHash::Sha1)
                           .toBase64();

    QString path = m_url.path(QUrl::FullyEncoded);
    if (path.isEmpty()) {
        path = QStringLiteral("/");
    }
    const QString query = m_url.query(QUrl::FullyEncoded);
    if (!query.isEmpty()) {
        path += QLatin1Char('?') + query;
    }

    const QString host = m_url.host();
    const int port = m_url.port();
    const QString hostHeader = m_url.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0
        ? (port == 443 ? host : QStringLiteral("%1:%2").arg(host).arg(port))
        : (port == 80 ? host : QStringLiteral("%1:%2").arg(host).arg(port));

    QByteArray request;
    request += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + hostHeader.toUtf8() + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";
    m_socket->write(request);
}

void WebSocketConnection::onReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_buffer += m_socket->readAll();

    if (m_state == State::Handshaking) {
        processHandshake();
    }
    if (m_state == State::Open) {
        processFrames();
    }
}

void WebSocketConnection::processHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }

    const QByteArray headerBlock = m_buffer.left(end);
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        fail(QStringLiteral("handshake_empty"));
        return;
    }

    const QByteArray statusLine = lines.first().trimmed();
    if (!statusLine.startsWith("HTTP/1.1 101") && !statusLine.startsWith("HTTP/1.0 101")) {
        fail(QStringLiteral("handshake_http_error"));
        return;
    }

    const QByteArray upgrade = headerValue(lines, "Upgrade");
    const QByteArray accept = headerValue(lines, "Sec-WebSocket-Accept");
    if (upgrade.compare("websocket", Qt::CaseInsensitive) != 0
        || accept != m_expectedAccept) {
        fail(QStringLiteral("handshake_invalid expected=%1 got=%2 upgrade=%3")
                 .arg(QString::fromLatin1(m_expectedAccept),
                      QString::fromLatin1(accept), QString::fromLatin1(upgrade)));
        return;
    }

    m_state = State::Open;
    m_handshakeTimer->stop();
    emit opened();
}

void WebSocketConnection::processFrames()
{
    while (m_state == State::Open) {
        if (m_buffer.size() < 2) {
            return;
        }

        const quint8 first = static_cast<quint8>(m_buffer.at(0));
        const quint8 second = static_cast<quint8>(m_buffer.at(1));
        const bool fin = (first & 0x80) != 0;
        const int opcode = first & 0x0F;
        const bool masked = (second & 0x80) != 0;
        quint64 length = second & 0x7F;
        int offset = 2;

        if (length == 126) {
            if (m_buffer.size() < 4) {
                return;
            }
            length = (static_cast<quint8>(m_buffer.at(2)) << 8)
                | static_cast<quint8>(m_buffer.at(3));
            offset = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10) {
                return;
            }
            length = 0;
            for (int index = 0; index < 8; ++index) {
                length = (length << 8) | static_cast<quint8>(m_buffer.at(2 + index));
            }
            offset = 10;
        }

        if (length > kMaxFrameBytes) {
            fail(QStringLiteral("frame_too_large"));
            return;
        }
        if (masked) {
            fail(QStringLiteral("masked_server_frame"));
            return;
        }
        if (static_cast<quint64>(m_buffer.size()) < static_cast<quint64>(offset) + length) {
            return;
        }

        const QByteArray payload = m_buffer.mid(offset, static_cast<int>(length));
        m_buffer.remove(0, offset + static_cast<int>(length));
        processFrame(fin, opcode, payload);
    }
}

void WebSocketConnection::processFrame(bool fin, int opcode, const QByteArray &payload)
{
    if (opcode >= 0x8) {
        if (!fin || payload.size() > 125) {
            fail(QStringLiteral("invalid_control_frame"));
            return;
        }
        if (opcode == 0x8) {
            int code = 1005;
            QString reason;
            if (payload.size() >= 2) {
                code = (static_cast<quint8>(payload.at(0)) << 8)
                    | static_cast<quint8>(payload.at(1));
                reason = QString::fromUtf8(payload.mid(2));
            }
            m_closeCode = code;
            m_closeReason = reason;
            if (m_state == State::Open) {
                sendClose(code, reason);
                m_state = State::Closing;
                if (m_socket) {
                    m_socket->flush();
                    m_socket->disconnectFromHost();
                }
            }
        } else if (opcode == 0x9) {
            sendFrame(0xA, payload);
        } else if (opcode == 0xA) {
            emit pongReceived();
        } else {
            fail(QStringLiteral("unknown_control_opcode"));
        }
        return;
    }

    if (opcode == 0x2) {
        fail(QStringLiteral("binary_not_supported"));
        return;
    }

    if (opcode == 0x0) {
        if (m_fragmentOpcode == 0) {
            fail(QStringLiteral("unexpected_continuation"));
            return;
        }
        m_fragment += payload;
    } else if (opcode == 0x1) {
        if (m_fragmentOpcode != 0) {
            fail(QStringLiteral("nested_fragment"));
            return;
        }
        m_fragmentOpcode = opcode;
        m_fragment = payload;
    } else {
        fail(QStringLiteral("unknown_data_opcode"));
        return;
    }

    if (static_cast<quint64>(m_fragment.size()) > kMaxFrameBytes) {
        fail(QStringLiteral("frame_too_large"));
        return;
    }

    if (fin) {
        const int completeOpcode = m_fragmentOpcode;
        const QByteArray complete = m_fragment;
        m_fragment.clear();
        m_fragmentOpcode = 0;
        if (completeOpcode == 0x1) {
            emit textMessageReceived(QString::fromUtf8(complete));
        }
    }
}

void WebSocketConnection::sendText(const QString &text)
{
    if (m_state != State::Open) {
        return;
    }
    sendFrame(0x1, text.toUtf8());
}

void WebSocketConnection::sendPing(const QByteArray &payload)
{
    if (m_state != State::Open) {
        return;
    }
    sendFrame(0x9, payload.left(125));
}

void WebSocketConnection::sendClose(int code, const QString &reason)
{
    QByteArray payload;
    payload.append(static_cast<char>((code >> 8) & 0xFF));
    payload.append(static_cast<char>(code & 0xFF));
    payload += reason.toUtf8().left(123);
    sendFrame(0x8, payload);
}

void WebSocketConnection::sendFrame(int opcode, const QByteArray &payload)
{
    if (!m_socket || (m_state != State::Open && m_state != State::Closing)) {
        return;
    }

    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));

    const int length = payload.size();
    if (length < 126) {
        frame.append(static_cast<char>(0x80 | length));
    } else if (length <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((length >> 8) & 0xFF));
        frame.append(static_cast<char>(length & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((static_cast<quint64>(length) >> shift) & 0xFF));
        }
    }

    QByteArray mask = randomBytes(4);
    frame.append(mask);
    const int payloadOffset = frame.size();
    frame.append(payload);
    for (int index = 0; index < length; ++index) {
        frame[payloadOffset + index] =
            static_cast<char>(frame.at(payloadOffset + index) ^ mask.at(index % 4));
    }

    m_socket->write(frame);
}

void WebSocketConnection::close(int code, const QString &reason)
{
    if (m_state == State::Closed) {
        return;
    }
    m_closeCode = code;
    m_closeReason = reason;

    if (m_state == State::Open) {
        sendClose(code, reason);
        m_state = State::Closing;
        if (m_socket) {
            m_socket->flush();
            m_socket->disconnectFromHost();
        }
        return;
    }

    m_state = State::Closed;
    stopTimers();
    if (m_socket) {
        m_socket->abort();
    }
    if (!m_closedEmitted) {
        m_closedEmitted = true;
        emit closed(code, reason);
    }
}

void WebSocketConnection::fail(const QString &reason, bool tlsMismatch)
{
    if (m_state == State::Closed) {
        return;
    }
    m_state = State::Closed;
    stopTimers();
    emit transportError(reason, tlsMismatch);
    if (m_socket) {
        m_socket->abort();
    }
    if (!m_closedEmitted) {
        m_closedEmitted = true;
        emit closed(1006, reason);
    }
}

void WebSocketConnection::onDisconnected()
{
    if (m_state == State::Closed) {
        return;
    }
    m_state = State::Closed;
    stopTimers();
    if (!m_closedEmitted) {
        m_closedEmitted = true;
        emit closed(m_closeCode > 0 ? m_closeCode : 1006, m_closeReason);
    }
}

void WebSocketConnection::stopTimers()
{
    m_connectTimer->stop();
    m_handshakeTimer->stop();
}

} // namespace mini::net
