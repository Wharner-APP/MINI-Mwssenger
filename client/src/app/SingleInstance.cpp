#include "app/SingleInstance.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

#include <memory>

namespace mini {

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent), m_key(key)
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
    }
    if (m_lockFile) {
        m_lockFile->unlock();
    }
}

QString SingleInstance::channelName() const
{
    const QString user = qEnvironmentVariable("USERNAME");
    return QStringLiteral("%1-%2").arg(m_key, user.isEmpty() ? QStringLiteral("user") : user);
}

bool SingleInstance::tryLock()
{
    const QString lockPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(channelName() + QStringLiteral(".lock"));

    m_lockFile = std::make_unique<QLockFile>(lockPath);
    m_lockFile->setStaleLockTime(30000);
    if (!m_lockFile->tryLock(0)) {
        // Somebody owns the lock; ask the running instance to come forward.
        QLocalSocket socket;
        socket.connectToServer(channelName());
        if (socket.waitForConnected(300)) {
            socket.write("activate\n");
            socket.flush();
            socket.waitForBytesWritten(300);
            socket.disconnectFromServer();
        }
        return false;
    }

    // We own the instance lock. The local socket is only the activation
    // channel; its name is reused after a crash.
    m_server = new QLocalServer(this);
    QLocalServer::removeServer(channelName());
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server->listen(channelName())) {
        connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::handleConnections);
    }
    return true;
}

void SingleInstance::handleConnections()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            const QByteArray message = socket->readAll().trimmed();
            if (message == "activate") {
                emit secondInstanceStarted();
            }
            socket->disconnectFromServer();
        });
    }
}

} // namespace mini
