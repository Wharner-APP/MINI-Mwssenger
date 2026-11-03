#include "services/SessionCrypto.h"

#include "crypto/Sodium.h"

#include <QJsonDocument>
#include <QJsonValue>

#include <sodium.h>

namespace mini::services {

namespace {

constexpr auto kSessionVersion = 2;
constexpr auto kX3dhKey = "x3dh";
constexpr auto kRatchetKey = "dr";
constexpr auto kServerOnlyKey = "sender_login";

QString b64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray unb64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

// Deterministic associated data: a sorted JSON object without the field the
// server adds on delivery. Both sides derive the same bytes.
QByteArray associatedData(const QJsonObject &header)
{
    QJsonObject clean = header;
    clean.remove(QLatin1String(kServerOnlyKey));
    return QJsonDocument(clean).toJson(QJsonDocument::Compact);
}

QJsonObject introToJson(const crypto::X3DHInit &init)
{
    return QJsonObject{
        {QStringLiteral("ik"), b64(init.identityKey)},
        {QStringLiteral("ek"), b64(init.ephemeralKey)},
        {QStringLiteral("spk"), static_cast<int>(init.signedPrekeyId)},
        {QStringLiteral("opk"), static_cast<int>(init.oneTimePrekeyId)},
    };
}

Result<crypto::X3DHInit> introFromJson(const QJsonObject &object)
{
    crypto::X3DHInit init;
    init.identityKey = unb64(object.value(QStringLiteral("ik")));
    init.ephemeralKey = unb64(object.value(QStringLiteral("ek")));
    init.signedPrekeyId =
        static_cast<quint32>(object.value(QStringLiteral("spk")).toDouble(0));
    init.oneTimePrekeyId =
        static_cast<quint32>(object.value(QStringLiteral("opk")).toDouble(0));
    if (init.identityKey.size() != 32 || init.ephemeralKey.size() != 32
        || init.signedPrekeyId == 0) {
        return Result<crypto::X3DHInit>::error(QStringLiteral("peer_intro_invalid"));
    }
    return Result<crypto::X3DHInit>::ok(init);
}

QString mapX3dhError(const QString &error)
{
    if (error == QStringLiteral("crypto.x3dh.signature")) {
        return QStringLiteral("peer_prekey_signature");
    }
    return error;
}

} // namespace

struct SessionCrypto::SessionState
{
    std::shared_ptr<crypto::DoubleRatchet> ratchet;
    bool pendingIntro = false;
    crypto::X3DHInit intro;
};

SessionCrypto::SessionCrypto(const crypto::IdentityState &identity, PeerKeyResolver resolver)
    : m_identity(identity), m_resolver(std::move(resolver))
{
}

bool SessionCrypto::isReady(const QString &chatId) const
{
    return m_sessions.contains(chatId);
}

void SessionCrypto::setPeerBundle(const QString &chatId, const crypto::PrekeyBundle &bundle)
{
    auto session = startInitiator(bundle);
    if (session.isOk()) {
        m_sessions.insert(chatId, session.value());
    }
}

void SessionCrypto::setPeerKeys(const QString &chatId, const QByteArray &key)
{
    // Only tests use this; a real peer always supplies an X3DH bundle.
    crypto::X25519KeyPair pair;
    pair.publicKey = key.left(32);
    pair.privateKey = QByteArray(32, '\x11');
    if (pair.publicKey.size() != 32) {
        return;
    }
    const QByteArray seed(32, '\x5a');
    auto ratchet = crypto::DoubleRatchet::initInitiator(seed, pair, key);
    if (ratchet.isError()) {
        return;
    }
    auto state = std::make_shared<SessionState>();
    state->ratchet = std::make_shared<crypto::DoubleRatchet>(std::move(ratchet.value()));
    m_sessions.insert(chatId, state);
}

Result<crypto::PrekeyBundle> SessionCrypto::bundleFromKeys(const net::PeerKeys &peer) const
{
    if (peer.identityKey.size() != 32 || peer.signedPrekey.size() != 32
        || peer.signedPrekeySignature.size() != 64) {
        return Result<crypto::PrekeyBundle>::error(QStringLiteral("peer_keys_invalid"));
    }
    crypto::PrekeyBundle bundle;
    bundle.identityKey = peer.identityKey;
    bundle.signedPrekeyId = peer.signedPrekeyId != 0 ? peer.signedPrekeyId : 1;
    bundle.signedPrekey = peer.signedPrekey;
    bundle.signedPrekeySignature = peer.signedPrekeySignature;
    if (peer.oneTimePrekeyId != 0 && peer.oneTimePrekey.size() == 32) {
        bundle.oneTimePrekeyId = peer.oneTimePrekeyId;
        bundle.oneTimePrekey = peer.oneTimePrekey;
    }
    return Result<crypto::PrekeyBundle>::ok(bundle);
}

Result<std::shared_ptr<SessionCrypto::SessionState>> SessionCrypto::startInitiator(
    const crypto::PrekeyBundle &bundle)
{
    if (!crypto::Sodium::init()) {
        return Result<std::shared_ptr<SessionState>>::error(QStringLiteral("crypto_unavailable"));
    }
    const Result<crypto::X3DHResult> x3dh =
        crypto::X3DH::initiate(bundle, m_identity.identity);
    if (x3dh.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(mapX3dhError(x3dh.error()));
    }
    auto ratchet = crypto::DoubleRatchet::initInitiator(
        x3dh.value().sharedSecret, x3dh.value().ephemeral, bundle.signedPrekey);
    if (ratchet.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(ratchet.error());
    }
    auto state = std::make_shared<SessionState>();
    state->ratchet = std::make_shared<crypto::DoubleRatchet>(std::move(ratchet.value()));
    state->pendingIntro = true;
    state->intro = x3dh.value().init;
    return Result<std::shared_ptr<SessionState>>::ok(state);
}

Result<std::shared_ptr<SessionCrypto::SessionState>> SessionCrypto::startResponder(
    const QJsonObject &introObject)
{
    const Result<crypto::X3DHInit> intro = introFromJson(introObject);
    if (intro.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(intro.error());
    }
    const Result<crypto::SignedPrekey> signedPrekey =
        crypto::IdentityKeys::signedPrekeyById(m_identity, intro.value().signedPrekeyId);
    if (signedPrekey.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(signedPrekey.error());
    }
    const Result<QByteArray> shared =
        crypto::X3DH::respond(intro.value(), m_identity);
    if (shared.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(mapX3dhError(shared.error()));
    }
    auto ratchet = crypto::DoubleRatchet::initResponder(
        shared.value(), signedPrekey.value().pair, intro.value().ephemeralKey);
    if (ratchet.isError()) {
        return Result<std::shared_ptr<SessionState>>::error(ratchet.error());
    }
    // The server hands each one-time prekey out exactly once; forget the
    // private half locally as well.
    if (intro.value().oneTimePrekeyId != 0) {
        crypto::IdentityKeys::consumeOneTimePrekey(m_identity, intro.value().oneTimePrekeyId);
    }
    auto state = std::make_shared<SessionState>();
    state->ratchet = std::make_shared<crypto::DoubleRatchet>(std::move(ratchet.value()));
    return Result<std::shared_ptr<SessionState>>::ok(state);
}

void SessionCrypto::prepare(const QString &chatId, const QString &peerLogin,
                            std::function<void(Result<bool>)> done)
{
    if (m_sessions.contains(chatId)) {
        done(Result<bool>::ok(true));
        return;
    }
    if (!m_resolver) {
        done(Result<bool>::error(QStringLiteral("peer_key_resolver_missing")));
        return;
    }
    if (peerLogin.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("peer_login_missing")));
        return;
    }

    m_resolver(peerLogin, [this, chatId, done = std::move(done)](Result<PeerKeys> peer) {
        if (peer.isError()) {
            done(Result<bool>::error(peer.error()));
            return;
        }
        const auto bundle = bundleFromKeys(peer.value());
        if (bundle.isError()) {
            done(Result<bool>::error(bundle.error()));
            return;
        }
        auto session = startInitiator(bundle.value());
        if (session.isError()) {
            done(Result<bool>::error(session.error()));
            return;
        }
        m_sessions.insert(chatId, session.value());
        done(Result<bool>::ok(true));
    });
}

Result<QByteArray> SessionCrypto::encrypt(const QString &chatId, const QByteArray &plaintext,
                                          QJsonObject *header)
{
    if (!header) {
        return Result<QByteArray>::error(QStringLiteral("header_required"));
    }
    auto session = m_sessions.find(chatId);
    if (session == m_sessions.end()) {
        return Result<QByteArray>::error(QStringLiteral("crypto_not_ready"));
    }
    SessionState &state = *session.value();

    header->insert(QStringLiteral("session_v"), kSessionVersion);
    if (state.pendingIntro) {
        header->insert(QLatin1String(kX3dhKey), introToJson(state.intro));
    }
    const crypto::RatchetHeader next = state.ratchet->nextHeader();
    const QByteArray serialized = next.serialize();
    if (serialized.isEmpty()) {
        return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.inactive"));
    }
    header->insert(QLatin1String(kRatchetKey), b64(serialized));

    const QByteArray aad = associatedData(*header);
    auto message = state.ratchet->encrypt(plaintext, aad);
    if (message.isError()) {
        return Result<QByteArray>::error(message.error());
    }
    state.pendingIntro = false;
    return Result<QByteArray>::ok(message.value().ciphertext);
}

Result<QByteArray> SessionCrypto::decrypt(const QString &chatId, const QByteArray &ciphertext,
                                          const QJsonObject &header)
{
    auto session = m_sessions.find(chatId);
    if (session == m_sessions.end()) {
        const QJsonValue intro = header.value(QLatin1String(kX3dhKey));
        if (!intro.isObject()) {
            return Result<QByteArray>::error(QStringLiteral("crypto_not_ready"));
        }
        auto started = startResponder(intro.toObject());
        if (started.isError()) {
            return Result<QByteArray>::error(started.error());
        }
        m_sessions.insert(chatId, started.value());
        session = m_sessions.find(chatId);
    }

    const Result<crypto::RatchetHeader> ratchetHeader =
        crypto::RatchetHeader::parse(unb64(header.value(QLatin1String(kRatchetKey))));
    if (ratchetHeader.isError()) {
        return Result<QByteArray>::error(ratchetHeader.error());
    }
    crypto::RatchetMessage message;
    message.header = ratchetHeader.value();
    message.ciphertext = ciphertext;

    const QByteArray aad = associatedData(header);
    return session.value()->ratchet->decrypt(message, aad);
}

} // namespace mini::services
