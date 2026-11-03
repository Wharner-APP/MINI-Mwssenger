#include "crypto/SenderKey.h"

#include "crypto/Sodium.h"

#include <QJsonDocument>
#include <QJsonValue>

#include <sodium.h>

#include <cstring>

namespace mini::crypto {
namespace {

constexpr int kKeyBytes = 32;
constexpr int kNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr int kTagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;

QByteArray hmacSha256(const QByteArray &key, quint8 tag)
{
    unsigned char out[crypto_auth_hmacsha256_BYTES];
    const unsigned char message[1] = {tag};
    crypto_auth_hmacsha256(out, message, 1,
                           reinterpret_cast<const unsigned char *>(key.constData()));
    return QByteArray(reinterpret_cast<const char *>(out), crypto_auth_hmacsha256_BYTES);
}

QString base64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray fromBase64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

} // namespace

bool SenderKeyState::isValid() const
{
    return keyId > 0 && chainKey.size() == kKeyBytes
        && signingPublic.size() == crypto_sign_PUBLICKEYBYTES;
}

QJsonObject SenderKeyHeader::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("group_id"), groupId);
    object.insert(QStringLiteral("sender_id"), senderId);
    object.insert(QStringLiteral("key_id"), static_cast<double>(keyId));
    object.insert(QStringLiteral("iteration"), static_cast<double>(iteration));
    object.insert(QStringLiteral("prev_iteration"), static_cast<double>(previousIteration));
    return object;
}

Result<SenderKeyHeader> SenderKeyHeader::fromJson(const QJsonObject &object)
{
    const QString groupId = object.value(QStringLiteral("group_id")).toString();
    const QString senderId = object.value(QStringLiteral("sender_id")).toString();
    const QJsonValue keyId = object.value(QStringLiteral("key_id"));
    const QJsonValue iteration = object.value(QStringLiteral("iteration"));
    if (groupId.isEmpty() || senderId.isEmpty() || !keyId.isDouble()
        || !iteration.isDouble()) {
        return Result<SenderKeyHeader>::error(QStringLiteral("sender_key_header_invalid"));
    }
    SenderKeyHeader header;
    header.groupId = groupId;
    header.senderId = senderId;
    header.keyId = static_cast<quint32>(keyId.toDouble());
    header.iteration = static_cast<quint32>(iteration.toDouble());
    header.previousIteration = static_cast<quint32>(
        object.value(QStringLiteral("prev_iteration")).toDouble(0));
    return Result<SenderKeyHeader>::ok(header);
}

QByteArray SenderKeyHeader::aad() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
}

Result<SenderKeyState> SenderKey::generate(quint32 keyId)
{
    if (!Sodium::init()) {
        return Result<SenderKeyState>::error(QStringLiteral("crypto_unavailable"));
    }
    SenderKeyState state;
    state.keyId = keyId == 0 ? 1 : keyId;
    state.chainKey = Sodium::randomBytes(kKeyBytes);
    if (state.chainKey.size() != kKeyBytes) {
        return Result<SenderKeyState>::error(QStringLiteral("crypto_random_failed"));
    }
    unsigned char publicKey[crypto_sign_PUBLICKEYBYTES];
    unsigned char privateKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(publicKey, privateKey);
    state.signingPublic =
        QByteArray(reinterpret_cast<const char *>(publicKey), crypto_sign_PUBLICKEYBYTES);
    state.signingPrivate =
        QByteArray(reinterpret_cast<const char *>(privateKey), crypto_sign_SECRETKEYBYTES);
    return Result<SenderKeyState>::ok(state);
}

Result<SenderKeyState> SenderKey::rotate(const SenderKeyState &current)
{
    return generate(current.keyId + 1);
}

QByteArray SenderKey::messageKey(const QByteArray &chainKey)
{
    return hmacSha256(chainKey, 0x01);
}

QByteArray SenderKey::nextChainKey(const QByteArray &chainKey)
{
    return hmacSha256(chainKey, 0x02);
}

Result<QByteArray> SenderKey::encrypt(SenderKeyState &state, const QString &groupId,
                                      const QString &senderId,
                                      const QByteArray &plaintext, QJsonObject *headerOut)
{
    if (!state.isValid() || !state.canSign() || groupId.isEmpty() || senderId.isEmpty()) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_state_invalid"));
    }

    SenderKeyHeader header;
    header.groupId = groupId;
    header.senderId = senderId;
    header.keyId = state.keyId;
    header.iteration = state.iteration;
    header.previousIteration = state.previousIteration;
    const QByteArray aad = header.aad();

    const QByteArray messageKeyBytes = messageKey(state.chainKey);
    const QByteArray nextKey = nextChainKey(state.chainKey);

    QByteArray ciphertext(plaintext.size() + kTagBytes, Qt::Uninitialized);
    unsigned char nonce[kNonceBytes] = {};
    unsigned long long cipherLength = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLength,
        reinterpret_cast<const unsigned char *>(plaintext.constData()),
        static_cast<unsigned long long>(plaintext.size()),
        reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nullptr, nonce,
        reinterpret_cast<const unsigned char *>(messageKeyBytes.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_encrypt_failed"));
    }
    ciphertext.resize(static_cast<int>(cipherLength));

    QByteArray signedData = aad + ciphertext;
    unsigned char signature[crypto_sign_BYTES];
    unsigned long long signatureLength = 0;
    crypto_sign_detached(
        signature, &signatureLength,
        reinterpret_cast<const unsigned char *>(signedData.constData()),
        static_cast<unsigned long long>(signedData.size()),
        reinterpret_cast<const unsigned char *>(state.signingPrivate.constData()));

    state.previousIteration = state.iteration;
    state.chainKey = nextKey;
    ++state.iteration;

    if (headerOut) {
        *headerOut = header.toJson();
        headerOut->insert(QStringLiteral("sig"),
                          base64(QByteArray(reinterpret_cast<const char *>(signature),
                                            static_cast<int>(signatureLength))));
    }
    return Result<QByteArray>::ok(ciphertext);
}

Result<QByteArray> SenderKey::decrypt(SenderKeyState &state, const SenderKeyHeader &header,
                                      const QByteArray &ciphertext,
                                      const QByteArray &signature)
{
    if (!state.isValid() || header.keyId != state.keyId
        || header.groupId.isEmpty() || header.senderId.isEmpty()) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_unknown"));
    }
    if (signature.size() != crypto_sign_BYTES || ciphertext.size() < kTagBytes) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_signature_invalid"));
    }
    if (header.iteration < state.iteration && !state.skipped.contains(header.iteration)) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_message_old"));
    }
    if (header.iteration >= state.iteration + SenderKeyState::kMaxSkipped) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_skip_limit"));
    }

    const QByteArray aad = header.aad();
    QByteArray signedData = aad + ciphertext;
    if (crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(signedData.constData()),
            static_cast<unsigned long long>(signedData.size()),
            reinterpret_cast<const unsigned char *>(state.signingPublic.constData()))
        != 0) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_signature_invalid"));
    }

    // Work on copies so a failed AEAD does not advance the chain.
    QByteArray chain = state.chainKey;
    quint32 iteration = state.iteration;
    QHash<quint32, QByteArray> skipped = state.skipped;
    QByteArray messageKeyBytes;
    if (header.iteration < iteration) {
        messageKeyBytes = skipped.take(header.iteration);
    } else {
        while (iteration < header.iteration) {
            skipped.insert(iteration, messageKey(chain));
            if (skipped.size() > SenderKeyState::kMaxSkipped) {
                return Result<QByteArray>::error(QStringLiteral("sender_key_skip_limit"));
            }
            chain = nextChainKey(chain);
            ++iteration;
        }
        messageKeyBytes = messageKey(chain);
        chain = nextChainKey(chain);
        ++iteration;
    }

    QByteArray plaintext(ciphertext.size(), Qt::Uninitialized);
    unsigned long long plainLength = 0;
    unsigned char nonce[kNonceBytes] = {};
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plaintext.data()), &plainLength, nullptr,
        reinterpret_cast<const unsigned char *>(ciphertext.constData()),
        static_cast<unsigned long long>(ciphertext.size()),
        reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nonce,
        reinterpret_cast<const unsigned char *>(messageKeyBytes.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_decrypt_failed"));
    }
    plaintext.resize(static_cast<int>(plainLength));

    state.chainKey = chain;
    state.iteration = iteration;
    state.skipped = skipped;
    return Result<QByteArray>::ok(plaintext);
}

QByteArray SenderKey::serializeDistribution(const QString &groupId, const QString &senderId,
                                            const SenderKeyState &state)
{
    QJsonObject object;
    object.insert(QStringLiteral("group_id"), groupId);
    object.insert(QStringLiteral("sender_id"), senderId);
    object.insert(QStringLiteral("key_id"), static_cast<double>(state.keyId));
    object.insert(QStringLiteral("iteration"), static_cast<double>(state.iteration));
    object.insert(QStringLiteral("chain_key_b64"), base64(state.chainKey));
    object.insert(QStringLiteral("signing_public_b64"), base64(state.signingPublic));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

Result<SenderKeyState> SenderKey::parseDistribution(const QByteArray &payload,
                                                    const QString &expectedSenderId)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<SenderKeyState>::error(QStringLiteral("sender_key_payload_invalid"));
    }
    const QJsonObject object = document.object();
    const QString senderId = object.value(QStringLiteral("sender_id")).toString();
    if (!expectedSenderId.isEmpty() && senderId != expectedSenderId) {
        return Result<SenderKeyState>::error(QStringLiteral("sender_key_sender_mismatch"));
    }
    SenderKeyState state;
    state.keyId = static_cast<quint32>(
        object.value(QStringLiteral("key_id")).toDouble(0));
    state.iteration = static_cast<quint32>(
        object.value(QStringLiteral("iteration")).toDouble(0));
    state.chainKey = fromBase64(object.value(QStringLiteral("chain_key_b64")));
    state.signingPublic = fromBase64(object.value(QStringLiteral("signing_public_b64")));
    if (!state.isValid()) {
        return Result<SenderKeyState>::error(QStringLiteral("sender_key_payload_invalid"));
    }
    return Result<SenderKeyState>::ok(state);
}

} // namespace mini::crypto
