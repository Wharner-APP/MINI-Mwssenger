#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace mini::services {

// Seam between MessageService and GroupService. MessageService owns the
// pipeline; GroupService owns the per-group sender key states.
class GroupCryptoSession
{
public:
    virtual ~GroupCryptoSession() = default;

    // Load or create the local sender key for this group. Must complete before
    // encryptGroup() can succeed for the first message.
    virtual void prepareGroup(const QString &groupId,
                              std::function<void(Result<bool>)> done) = 0;

    // Load the sender's distribution for a received message. `senderLogin` is
    // the authenticated login from the envelope header.
    virtual void prepareSenderKey(const QString &groupId, const QString &senderLogin,
                                  std::function<void(Result<bool>)> done) = 0;

    [[nodiscard]] virtual bool isGroupReady(const QString &groupId) const = 0;

    virtual Result<QByteArray> encryptGroup(const QString &groupId,
                                            const QByteArray &plaintext,
                                            QJsonObject *header) = 0;
    virtual Result<QByteArray> decryptGroup(const QString &groupId,
                                            const QString &senderLogin,
                                            const QByteArray &ciphertext,
                                            const QJsonObject &header) = 0;
};

} // namespace mini::services
