#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace mini::storage {

// Field-level encryption for message bodies (§4.5): XChaCha20-Poly1305 with a
// random 24-byte nonce. Blob layout: [version=1][nonce][ciphertext+tag].
class BodyCipher
{
public:
    explicit BodyCipher(const QByteArray &key);

    [[nodiscard]] bool isValid() const { return m_valid; }

    Result<QByteArray> encrypt(const QString &plaintext, const QByteArray &aad) const;
    Result<QString> decrypt(const QByteArray &sealed, const QByteArray &aad) const;

private:
    QByteArray m_key;
    bool m_valid = false;
};

// Binds a sealed body to its message so a row cannot be swapped between chats.
[[nodiscard]] QByteArray messageAad(const QString &chatId, const QString &clientMsgId);

// Binds an encrypted chat preview to its chat.
[[nodiscard]] QByteArray chatPreviewAad(const QString &chatId);

} // namespace mini::storage
