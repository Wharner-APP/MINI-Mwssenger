#pragma once

#include <QByteArray>

#include <cstddef>

namespace mini::crypto {

// Thin, fail-closed wrapper around the process-wide libsodium initialisation
// plus the three helpers every other crypto module needs. Nothing here
// implements an algorithm: all primitives come from libsodium.
class Sodium
{
public:
    Sodium() = delete;

    // Idempotent and thread-safe. Returns false if libsodium refused to start.
    static bool init();
    static bool isAvailable();

    // CSPRNG bytes. Returns an empty array for count <= 0 or if init failed.
    [[nodiscard]] static QByteArray randomBytes(int count);

    // Overwrites the buffer with zeros in a way the optimiser cannot drop.
    static void memzero(QByteArray &bytes);
    static void memzero(void *data, std::size_t size);

    // Constant-time comparison for equal-length byte strings.
    [[nodiscard]] static bool constantTimeEquals(const QByteArray &a, const QByteArray &b);

    [[nodiscard]] static QByteArray toBase64(const QByteArray &bytes);
    [[nodiscard]] static QByteArray fromBase64(const QByteArray &text);
};

} // namespace mini::crypto
