#pragma once

#include "core/Result.h"
#include "crypto/DoubleRatchet.h"
#include "crypto/IdentityKeys.h"
#include "crypto/X3DH.h"
#include "net/ApiTypes.h"
#include "services/MessageService.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>

namespace mini::services {

// End-to-end session: X3DH establishes the shared secret from the peer's
// published bundle, then every message travels through a Double Ratchet.
// The ratchet header and the X3DH preamble are carried in the envelope header
// and authenticated as AAD, so neither can be swapped in transit.
class SessionCrypto : public CryptoSession
{
public:
    using PeerKeys = net::PeerKeys;
    using PeerKeyResult = std::function<void(Result<PeerKeys>)>;
    using PeerKeyResolver = std::function<void(const QString &login, PeerKeyResult done)>;

    SessionCrypto(const crypto::IdentityState &identity, PeerKeyResolver resolver);

    [[nodiscard]] bool isReady(const QString &chatId) const override;
    void prepare(const QString &chatId, const QString &peerLogin,
                 std::function<void(Result<bool>)> done) override;

    Result<QByteArray> encrypt(const QString &chatId, const QByteArray &plaintext,
                               QJsonObject *header) override;
    Result<QByteArray> decrypt(const QString &chatId, const QByteArray &ciphertext,
                               const QJsonObject &header) override;

    // Test seam: install a peer bundle without a network round-trip.
    void setPeerBundle(const QString &chatId, const crypto::PrekeyBundle &bundle);
    // Legacy seam kept for callers that only have a raw shared key: it starts
    // a ratchet with a synthetic identity, never used in production.
    void setPeerKeys(const QString &chatId, const QByteArray &key);

private:
    struct SessionState;

    [[nodiscard]] Result<crypto::PrekeyBundle> bundleFromKeys(const net::PeerKeys &peer) const;
    [[nodiscard]] Result<std::shared_ptr<SessionState>> startInitiator(
        const crypto::PrekeyBundle &bundle);
    [[nodiscard]] Result<std::shared_ptr<SessionState>> startResponder(
        const QJsonObject &intro);

    crypto::IdentityState m_identity;
    PeerKeyResolver m_resolver;
    QHash<QString, std::shared_ptr<SessionState>> m_sessions;
};

} // namespace mini::services
