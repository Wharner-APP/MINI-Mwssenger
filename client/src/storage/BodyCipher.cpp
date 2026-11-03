#include "storage/BodyCipher.h"

#include <sodium.h>

namespace mini::storage {
namespace {

constexpr int kNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr int kTagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr quint8 kFormatVersion = 1;
constexpr int kHeaderBytes = 1 + kNonceBytes;

} // namespace

BodyCipher::BodyCipher(const QByteArray &key) : m_key(key)
{
    m_valid = m_key.size() == crypto_aead_xchacha20poly1305_ietf_KEYBYTES
              && sodium_init() >= 0;
}

Result<QByteArray> BodyCipher::encrypt(const QString &plaintext, const QByteArray &aad) const
{
    if (!m_valid) {
        return Result<QByteArray>::error(QStringLiteral("message cipher is not configured"));
    }

    const QByteArray utf8 = plaintext.toUtf8();
    QByteArray sealed(kHeaderBytes + utf8.size() + kTagBytes, '\0');
    sealed[0] = static_cast<char>(kFormatVersion);

    auto *nonce = reinterpret_cast<unsigned char *>(sealed.data() + 1);
    randombytes_buf(nonce, kNonceBytes);

    unsigned long long cipherLength = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(sealed.data() + kHeaderBytes), &cipherLength,
        reinterpret_cast<const unsigned char *>(utf8.constData()),
        static_cast<unsigned long long>(utf8.size()),
        reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nullptr, nonce,
        reinterpret_cast<const unsigned char *>(m_key.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("message encryption failed"));
    }
    sealed.resize(kHeaderBytes + static_cast<int>(cipherLength));
    return Result<QByteArray>::ok(sealed);
}

Result<QString> BodyCipher::decrypt(const QByteArray &sealed, const QByteArray &aad) const
{
    if (!m_valid) {
        return Result<QString>::error(QStringLiteral("message cipher is not configured"));
    }
    if (sealed.size() < kHeaderBytes + kTagBytes
        || static_cast<quint8>(sealed.at(0)) != kFormatVersion) {
        return Result<QString>::error(QStringLiteral("sealed message has an unknown format"));
    }

    const auto *nonce =
        reinterpret_cast<const unsigned char *>(sealed.constData() + 1);
    QByteArray plaintext(sealed.size() - kHeaderBytes - kTagBytes, '\0');
    unsigned long long plainLength = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plaintext.data()), &plainLength, nullptr,
        reinterpret_cast<const unsigned char *>(sealed.constData() + kHeaderBytes),
        static_cast<unsigned long long>(sealed.size() - kHeaderBytes),
        reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nonce,
        reinterpret_cast<const unsigned char *>(m_key.constData()));
    if (rc != 0) {
        return Result<QString>::error(QStringLiteral("message authentication failed"));
    }
    plaintext.resize(static_cast<int>(plainLength));
    return Result<QString>::ok(QString::fromUtf8(plaintext));
}

QByteArray messageAad(const QString &chatId, const QString &clientMsgId)
{
    QByteArray aad;
    aad.reserve(chatId.size() + clientMsgId.size() + 1);
    aad.append(chatId.toUtf8());
    aad.append('\x1f');
    aad.append(clientMsgId.toUtf8());
    return aad;
}

QByteArray chatPreviewAad(const QString &chatId)
{
    QByteArray aad;
    aad.reserve(chatId.size() + 8);
    aad.append(chatId.toUtf8());
    aad.append(QByteArrayLiteral("\x1fpreview"));
    return aad;
}

} // namespace mini::storage
