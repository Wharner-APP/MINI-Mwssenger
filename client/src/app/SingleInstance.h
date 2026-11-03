#pragma once

#include <QObject>

#include <memory>

class QLocalServer;
class QLockFile;

namespace mini {

// One running client per user. A second launch asks the first one to come
// forward instead of starting another process.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &key = QStringLiteral("mini-messenger"),
                            QObject *parent = nullptr);
    ~SingleInstance() override;

    [[nodiscard]] bool tryLock();
    [[nodiscard]] QString channelName() const;

signals:
    void secondInstanceStarted();

private:
    void handleConnections();

    QString m_key;
    QLocalServer *m_server = nullptr;
    std::unique_ptr<QLockFile> m_lockFile;
};

} // namespace mini
