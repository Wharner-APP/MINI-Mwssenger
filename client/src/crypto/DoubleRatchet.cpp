#include "crypto/DoubleRatchet.h"

#include "crypto/Kdf.h"
#include "crypto/Sodium.h"

#include <QHash>
#include <QList>

#include <sodium.h>

namespace mini::crypto {

namespace {

constexpr int kKeyBytes = 32;
constexpr int kAeadTagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr int kNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;

void appendU32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xff));
    }
}

quint32 readU32(const unsigned char *p)
{
    return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8)
        | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
}

QByteArray clone(const QByteArray &source)
{
    if (source.isEmpty()) {
        return {};
    }
    return QByteArray(source.constData(), source.size());
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        sodium_memzero(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }
}

QByteArray zeroNonce()
{
    return QByteArray(kNonceBytes, '\0');
}

Result<QByteArray> seal(const QByteArray &key, const QByteArray &associatedData,
                        const QByteArray &plaintext)
{
    QByteArray cipher(plaintext.size() + kAeadTagBytes, Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    const QByteArray nonce = zeroNonce();
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(cipher.data()), &cipherLen,
        reinterpret_cast<const unsigned char *>(plaintext.constData()),
        static_cast<unsigned long long>(plaintext.size()),
        reinterpret_cast<const unsigned char *>(associatedData.constData()),
        static_cast<unsigned long long>(associatedData.size()), nullptr,
        reinterpret_cast<const unsigned char *>(nonce.constData()),
        reinterpret_cast<const unsigned char *>(key.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("crypto.aead_failed"));
    }
    cipher.resize(static_cast<int>(cipherLen));
    return Result<QByteArray>::ok(cipher);
}

Result<QByteArray> open(const QByteArray &key, const QByteArray &associatedData,
                        const QByteArray &cipher)
{
    if (cipher.size() < kAeadTagBytes) {
        return Result<QByteArray>::error(QStringLiteral("crypto.aead_failed"));
    }
    QByteArray plain(cipher.size() - kAeadTagBytes, Qt::Uninitialized);
    unsigned long long plainLen = 0;
    const QByteArray nonce = zeroNonce();
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plain.data()), &plainLen, nullptr,
        reinterpret_cast<const unsigned char *>(cipher.constData()),
        static_cast<unsigned long long>(cipher.size()),
        reinterpret_cast<const unsigned char *>(associatedData.constData()),
        static_cast<unsigned long long>(associatedData.size()),
        reinterpret_cast<const unsigned char *>(nonce.constData()),
        reinterpret_cast<const unsigned char *>(key.constData()));
    if (rc != 0 || plainLen != static_cast<unsigned long long>(plain.size())) {
        wipe(plain);
        return Result<QByteArray>::error(QStringLiteral("crypto.aead_failed"));
    }
    return Result<QByteArray>::ok(plain);
}

struct ChainStep
{
    QByteArray messageKey;
    QByteArray nextChainKey;
};

Result<ChainStep> kdfChain(const QByteArray &chainKey)
{
    if (chainKey.size() != kKeyBytes) {
        return Result<ChainStep>::error(QStringLiteral("crypto.ratchet.state"));
    }
    ChainStep step;
    const char messageLabel = 0x01;
    const char chainLabel = 0x02;
    step.messageKey = QByteArray(crypto_auth_hmacsha256_BYTES, Qt::Uninitialized);
    step.nextChainKey = QByteArray(crypto_auth_hmacsha256_BYTES, Qt::Uninitialized);
    crypto_auth_hmacsha256(
        reinterpret_cast<unsigned char *>(step.messageKey.data()),
        reinterpret_cast<const unsigned char *>(&messageLabel), 1,
        reinterpret_cast<const unsigned char *>(chainKey.constData()));
    crypto_auth_hmacsha256(
        reinterpret_cast<unsigned char *>(step.nextChainKey.data()),
        reinterpret_cast<const unsigned char *>(&chainLabel), 1,
        reinterpret_cast<const unsigned char *>(chainKey.constData()));
    return Result<ChainStep>::ok(std::move(step));
}

Result<X25519KeyPair> generateRatchetKey()
{
    X25519KeyPair pair;
    pair.publicKey = QByteArray(crypto_box_PUBLICKEYBYTES, Qt::Uninitialized);
    pair.privateKey = QByteArray(crypto_box_SECRETKEYBYTES, Qt::Uninitialized);
    if (crypto_box_keypair(reinterpret_cast<unsigned char *>(pair.publicKey.data()),
                           reinterpret_cast<unsigned char *>(pair.privateKey.data()))
        != 0) {
        return Result<X25519KeyPair>::error(QStringLiteral("crypto.ratchet.dh"));
    }
    return Result<X25519KeyPair>::ok(pair);
}

Result<QByteArray> ratchetDh(const QByteArray &secret, const QByteArray &publicKey)
{
    if (secret.size() != kKeyBytes || publicKey.size() != kKeyBytes) {
        return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.dh"));
    }
    QByteArray shared(kKeyBytes, Qt::Uninitialized);
    if (crypto_scalarmult_curve25519(
            reinterpret_cast<unsigned char *>(shared.data()),
            reinterpret_cast<const unsigned char *>(secret.constData()),
            reinterpret_cast<const unsigned char *>(publicKey.constData()))
        != 0) {
        wipe(shared);
        return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.dh"));
    }
    return Result<QByteArray>::ok(shared);
}

} // namespace

QByteArray RatchetHeader::serialize() const
{
    if (dhPublic.size() != kKeyBytes) {
        return {};
    }
    QByteArray out;
    out.reserve(kSerializedBytes);
    out.append(dhPublic);
    appendU32(out, previousChainLength);
    appendU32(out, messageNumber);
    return out;
}

Result<RatchetHeader> RatchetHeader::parse(const QByteArray &bytes)
{
    if (bytes.size() != kSerializedBytes) {
        return Result<RatchetHeader>::error(QStringLiteral("crypto.ratchet.header"));
    }
    RatchetHeader header;
    header.dhPublic = bytes.left(kKeyBytes);
    const auto *raw = reinterpret_cast<const unsigned char *>(bytes.constData());
    header.previousChainLength = readU32(raw + kKeyBytes);
    header.messageNumber = readU32(raw + kKeyBytes + 4);
    return Result<RatchetHeader>::ok(std::move(header));
}

QByteArray RatchetMessage::serialize() const
{
    const QByteArray headerBytes = header.serialize();
    if (headerBytes.isEmpty() || ciphertext.isEmpty()) {
        return {};
    }
    QByteArray out;
    out.reserve(headerBytes.size() + ciphertext.size());
    out.append(headerBytes);
    out.append(ciphertext);
    return out;
}

Result<RatchetMessage> RatchetMessage::parse(const QByteArray &bytes)
{
    if (bytes.size() < RatchetHeader::kSerializedBytes + kAeadTagBytes) {
        return Result<RatchetMessage>::error(QStringLiteral("crypto.ratchet.header"));
    }
    const Result<RatchetHeader> header =
        RatchetHeader::parse(bytes.left(RatchetHeader::kSerializedBytes));
    if (header.isError()) {
        return Result<RatchetMessage>::error(header.error());
    }
    RatchetMessage message;
    message.header = header.value();
    message.ciphertext = bytes.mid(RatchetHeader::kSerializedBytes);
    return Result<RatchetMessage>::ok(std::move(message));
}

struct DoubleRatchet::Impl
{
    bool active = false;
    QByteArray rootKey;
    QByteArray sendingChainKey;
    QByteArray receivingChainKey;
    X25519KeyPair selfRatchet;
    QByteArray remoteRatchet;
    QByteArray pendingRemoteRatchet; // responder: key promised by X3DH
    quint32 sendCount = 0;
    quint32 recvCount = 0;
    quint32 previousSendCount = 0;
    QHash<QByteArray, QByteArray> skippedKeys;
    QList<QByteArray> skippedOrder;

    ~Impl() { wipeAll(); }

    void wipeAll()
    {
        wipe(rootKey);
        wipe(sendingChainKey);
        wipe(receivingChainKey);
        wipe(selfRatchet.publicKey);
        wipe(selfRatchet.privateKey);
        wipe(remoteRatchet);
        wipe(pendingRemoteRatchet);
        for (auto it = skippedKeys.begin(); it != skippedKeys.end(); ++it) {
            wipe(it.value());
        }
        skippedKeys.clear();
        skippedOrder.clear();
        active = false;
    }

    void copyFrom(const Impl &other)
    {
        wipeAll();
        active = other.active;
        rootKey = clone(other.rootKey);
        sendingChainKey = clone(other.sendingChainKey);
        receivingChainKey = clone(other.receivingChainKey);
        selfRatchet.publicKey = clone(other.selfRatchet.publicKey);
        selfRatchet.privateKey = clone(other.selfRatchet.privateKey);
        remoteRatchet = clone(other.remoteRatchet);
        pendingRemoteRatchet = clone(other.pendingRemoteRatchet);
        sendCount = other.sendCount;
        recvCount = other.recvCount;
        previousSendCount = other.previousSendCount;
        skippedKeys.clear();
        skippedOrder = other.skippedOrder;
        for (auto it = other.skippedKeys.begin(); it != other.skippedKeys.end(); ++it) {
            skippedKeys.insert(it.key(), clone(it.value()));
        }
    }

    void storeSkipped(const QByteArray &dhPublic, quint32 number, const QByteArray &messageKey)
    {
        QByteArray key = dhPublic;
        appendU32(key, number);
        skippedKeys.insert(key, clone(messageKey));
        skippedOrder.append(key);
        while (skippedKeys.size() > DoubleRatchet::kMaxSkippedKeys) {
            const QByteArray oldest = skippedOrder.takeFirst();
            auto it = skippedKeys.find(oldest);
            if (it != skippedKeys.end()) {
                wipe(it.value());
                skippedKeys.erase(it);
            }
        }
    }

    Result<QByteArray> takeSkipped(const QByteArray &dhPublic, quint32 number)
    {
        QByteArray key = dhPublic;
        appendU32(key, number);
        auto it = skippedKeys.find(key);
        if (it == skippedKeys.end()) {
            return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.duplicate"));
        }
        QByteArray messageKey = clone(it.value());
        wipe(it.value());
        skippedKeys.erase(it);
        skippedOrder.removeOne(key);
        return Result<QByteArray>::ok(messageKey);
    }

    Result<int> skipMessageKeys(quint32 until)
    {
        if (until <= recvCount) {
            return Result<int>::ok(0);
        }
        if (receivingChainKey.size() != kKeyBytes) {
            return Result<int>::error(QStringLiteral("crypto_skip_limit"));
        }
        if (until - recvCount > static_cast<quint32>(DoubleRatchet::kMaxSkip)) {
            return Result<int>::error(QStringLiteral("crypto_skip_limit"));
        }
        while (recvCount < until) {
            Result<ChainStep> step = kdfChain(receivingChainKey);
            if (step.isError()) {
                return Result<int>::error(step.error());
            }
            storeSkipped(remoteRatchet, recvCount, step.value().messageKey);
            wipe(step.value().messageKey);
            receivingChainKey = step.value().nextChainKey;
            wipe(step.value().nextChainKey);
            ++recvCount;
        }
        return Result<int>::ok(0);
    }

    Result<int> dhRatchet(const QByteArray &remotePublic)
    {
        if (selfRatchet.privateKey.size() != kKeyBytes || remotePublic.size() != kKeyBytes) {
            return Result<int>::error(QStringLiteral("crypto.ratchet.state"));
        }
        previousSendCount = sendCount;
        sendCount = 0;
        recvCount = 0;

        Result<QByteArray> dh = ratchetDh(selfRatchet.privateKey, remotePublic);
        if (dh.isError()) {
            return Result<int>::error(dh.error());
        }
        const QByteArray info = QByteArrayLiteral("MINI/DoubleRatchet/v1");
        QByteArray rootAndChain = Kdf::hkdfSha256(dh.value(), rootKey, info, kKeyBytes * 2);
        wipe(dh.value());
        if (rootAndChain.size() != kKeyBytes * 2) {
            wipe(rootAndChain);
            return Result<int>::error(QStringLiteral("crypto.ratchet.kdf"));
        }
        receivingChainKey = QByteArray(rootAndChain.constData() + kKeyBytes, kKeyBytes);
        rootKey = QByteArray(rootAndChain.constData(), kKeyBytes);
        remoteRatchet = remotePublic;
        pendingRemoteRatchet.clear();
        wipe(rootAndChain);

        Result<X25519KeyPair> newPair = generateRatchetKey();
        if (newPair.isError()) {
            return Result<int>::error(newPair.error());
        }
        Result<QByteArray> dh2 = ratchetDh(newPair.value().privateKey, remotePublic);
        if (dh2.isError()) {
            return Result<int>::error(dh2.error());
        }
        QByteArray rootAndChain2 = Kdf::hkdfSha256(dh2.value(), rootKey, info, kKeyBytes * 2);
        wipe(dh2.value());
        if (rootAndChain2.size() != kKeyBytes * 2) {
            wipe(rootAndChain2);
            return Result<int>::error(QStringLiteral("crypto.ratchet.kdf"));
        }
        rootKey = QByteArray(rootAndChain2.constData(), kKeyBytes);
        sendingChainKey = QByteArray(rootAndChain2.constData() + kKeyBytes, kKeyBytes);
        wipe(rootAndChain2);
        selfRatchet = newPair.value();
        return Result<int>::ok(0);
    }

    Result<RatchetMessage> encryptMessage(const QByteArray &plaintext,
                                          const QByteArray &associatedData)
    {
        if (!active || sendingChainKey.size() != kKeyBytes) {
            return Result<RatchetMessage>::error(QStringLiteral("crypto.ratchet.inactive"));
        }
        RatchetMessage message;
        message.header.dhPublic = selfRatchet.publicKey;
        message.header.previousChainLength = previousSendCount;
        message.header.messageNumber = sendCount;
        QByteArray aad = message.header.serialize();
        aad.append(associatedData);
        if (aad.isEmpty()) {
            return Result<RatchetMessage>::error(QStringLiteral("crypto.ratchet.state"));
        }

        Result<ChainStep> step = kdfChain(sendingChainKey);
        if (step.isError()) {
            return Result<RatchetMessage>::error(step.error());
        }
        Result<QByteArray> cipher = seal(step.value().messageKey, aad, plaintext);
        wipe(step.value().messageKey);
        if (cipher.isError()) {
            wipe(step.value().nextChainKey);
            return Result<RatchetMessage>::error(cipher.error());
        }
        message.ciphertext = cipher.value();
        sendingChainKey = step.value().nextChainKey;
        wipe(step.value().nextChainKey);
        ++sendCount;
        return Result<RatchetMessage>::ok(std::move(message));
    }

    Result<QByteArray> decryptMessage(const RatchetMessage &message,
                                      const QByteArray &associatedData)
    {
        if (!active) {
            return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.inactive"));
        }
        if (message.header.dhPublic.size() != kKeyBytes
            || message.ciphertext.size() < kAeadTagBytes) {
            return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.header"));
        }

        Impl trial;
        trial.copyFrom(*this);

        // A message from the previous receiving chain may arrive after a DH
        // ratchet. Its key was stored as skipped under the old ratchet key,
        // so try (dhPublic, N) before switching chains.
        if (!trial.remoteRatchet.isEmpty()
            && !Sodium::constantTimeEquals(trial.remoteRatchet, message.header.dhPublic)) {
            Result<QByteArray> skipped = trial.takeSkipped(
                message.header.dhPublic, message.header.messageNumber);
            if (skipped.isOk()) {
                QByteArray aad = message.header.serialize();
                aad.append(associatedData);
                Result<QByteArray> plain = open(skipped.value(), aad, message.ciphertext);
                wipe(skipped.value());
                if (plain.isError()) {
                    return Result<QByteArray>::error(plain.error());
                }
                copyFrom(trial);
                return plain;
            }
        }

        if (!Sodium::constantTimeEquals(trial.remoteRatchet, message.header.dhPublic)) {
            if (trial.remoteRatchet.isEmpty()) {
                if (message.header.previousChainLength != 0) {
                    return Result<QByteArray>::error(QStringLiteral("crypto.ratchet.header"));
                }
                // The responder knows which key X3DH handed to the initiator;
                // the first ratchet header must use exactly that key.
                if (!trial.pendingRemoteRatchet.isEmpty()
                    && !Sodium::constantTimeEquals(trial.pendingRemoteRatchet,
                                                   message.header.dhPublic)) {
                    return Result<QByteArray>::error(
                        QStringLiteral("crypto.ratchet.peer_key"));
                }
            } else {
                const Result<int> skipped = trial.skipMessageKeys(
                    message.header.previousChainLength);
                if (skipped.isError()) {
                    return Result<QByteArray>::error(skipped.error());
                }
            }
            const Result<int> ratcheted = trial.dhRatchet(message.header.dhPublic);
            if (ratcheted.isError()) {
                return Result<QByteArray>::error(ratcheted.error());
            }
        }

        QByteArray messageKey;
        if (message.header.messageNumber < trial.recvCount) {
            Result<QByteArray> skipped =
                trial.takeSkipped(trial.remoteRatchet, message.header.messageNumber);
            if (skipped.isError()) {
                return Result<QByteArray>::error(skipped.error());
            }
            messageKey = skipped.value();
        } else {
            const Result<int> skipped =
                trial.skipMessageKeys(message.header.messageNumber);
            if (skipped.isError()) {
                return Result<QByteArray>::error(skipped.error());
            }
            Result<ChainStep> step = kdfChain(trial.receivingChainKey);
            if (step.isError()) {
                return Result<QByteArray>::error(step.error());
            }
            messageKey = step.value().messageKey;
            trial.receivingChainKey = step.value().nextChainKey;
            wipe(step.value().nextChainKey);
            ++trial.recvCount;
        }

        QByteArray aad = message.header.serialize();
        aad.append(associatedData);
        Result<QByteArray> plain = open(messageKey, aad, message.ciphertext);
        wipe(messageKey);
        if (plain.isError()) {
            return Result<QByteArray>::error(plain.error());
        }

        // The message authenticated: only now replace the live session state.
        copyFrom(trial);
        return plain;
    }
};

DoubleRatchet::DoubleRatchet() : m_impl(std::make_unique<Impl>()) {}

DoubleRatchet::~DoubleRatchet() = default;

DoubleRatchet::DoubleRatchet(DoubleRatchet &&other) noexcept = default;

DoubleRatchet &DoubleRatchet::operator=(DoubleRatchet &&other) noexcept = default;

Result<DoubleRatchet> DoubleRatchet::initInitiator(const QByteArray &sharedSecret,
                                                   const X25519KeyPair &ephemeral,
                                                   const QByteArray &peerSignedPrekey)
{
    if (!Sodium::init()) {
        return Result<DoubleRatchet>::error(QStringLiteral("crypto.init"));
    }
    if (sharedSecret.size() != kKeyBytes || !ephemeral.isValid()
        || peerSignedPrekey.size() != kKeyBytes) {
        return Result<DoubleRatchet>::error(QStringLiteral("crypto.ratchet.state"));
    }

    DoubleRatchet ratchet;
    Impl &impl = *ratchet.m_impl;
    impl.rootKey = clone(sharedSecret);
    impl.selfRatchet.publicKey = clone(ephemeral.publicKey);
    impl.selfRatchet.privateKey = clone(ephemeral.privateKey);
    impl.remoteRatchet = clone(peerSignedPrekey);

    Result<QByteArray> dh = ratchetDh(impl.selfRatchet.privateKey, impl.remoteRatchet);
    if (dh.isError()) {
        return Result<DoubleRatchet>::error(dh.error());
    }
    const QByteArray info = QByteArrayLiteral("MINI/DoubleRatchet/v1");
    QByteArray rootAndChain = Kdf::hkdfSha256(dh.value(), impl.rootKey, info, kKeyBytes * 2);
    wipe(dh.value());
    if (rootAndChain.size() != kKeyBytes * 2) {
        wipe(rootAndChain);
        return Result<DoubleRatchet>::error(QStringLiteral("crypto.ratchet.kdf"));
    }
    impl.rootKey = QByteArray(rootAndChain.constData(), kKeyBytes);
    impl.sendingChainKey = QByteArray(rootAndChain.constData() + kKeyBytes, kKeyBytes);
    wipe(rootAndChain);
    impl.active = true;
    return Result<DoubleRatchet>::ok(std::move(ratchet));
}

Result<DoubleRatchet> DoubleRatchet::initResponder(const QByteArray &sharedSecret,
                                                   const X25519KeyPair &signedPrekey,
                                                   const QByteArray &initiatorEphemeral)
{
    if (!Sodium::init()) {
        return Result<DoubleRatchet>::error(QStringLiteral("crypto.init"));
    }
    if (sharedSecret.size() != kKeyBytes || !signedPrekey.isValid()
        || initiatorEphemeral.size() != kKeyBytes) {
        return Result<DoubleRatchet>::error(QStringLiteral("crypto.ratchet.state"));
    }

    DoubleRatchet ratchet;
    Impl &impl = *ratchet.m_impl;
    impl.rootKey = clone(sharedSecret);
    impl.selfRatchet.publicKey = clone(signedPrekey.publicKey);
    impl.selfRatchet.privateKey = clone(signedPrekey.privateKey);
    // The responder learns the initiator's ratchet key from the first header;
    // it must match the ephemeral key X3DH used, so remember what to expect.
    impl.remoteRatchet.clear();
    impl.pendingRemoteRatchet = clone(initiatorEphemeral);
    impl.active = true;
    return Result<DoubleRatchet>::ok(std::move(ratchet));
}

RatchetHeader DoubleRatchet::nextHeader() const
{
    RatchetHeader header;
    if (!m_impl->active) {
        return header;
    }
    header.dhPublic = m_impl->selfRatchet.publicKey;
    header.previousChainLength = m_impl->previousSendCount;
    header.messageNumber = m_impl->sendCount;
    return header;
}

Result<RatchetMessage> DoubleRatchet::encrypt(const QByteArray &plaintext,
                                              const QByteArray &associatedData)
{
    return m_impl->encryptMessage(plaintext, associatedData);
}

Result<QByteArray> DoubleRatchet::decrypt(const RatchetMessage &message,
                                          const QByteArray &associatedData)
{
    return m_impl->decryptMessage(message, associatedData);
}

void DoubleRatchet::reset()
{
    m_impl->wipeAll();
}

bool DoubleRatchet::isActive() const
{
    return m_impl->active;
}

int DoubleRatchet::skippedKeyCount() const
{
    return static_cast<int>(m_impl->skippedKeys.size());
}

} // namespace mini::crypto
