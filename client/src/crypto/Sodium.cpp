#include "crypto/Sodium.h"

#include <sodium.h>

#include <mutex>

namespace mini::crypto {

namespace {

std::once_flag g_sodiumInit;
bool g_sodiumReady = false;

} // namespace

bool Sodium::init()
{
    std::call_once(g_sodiumInit, [] {
        g_sodiumReady = sodium_init() >= 0;
    });
    return g_sodiumReady;
}

bool Sodium::isAvailable()
{
    return init();
}

QByteArray Sodium::randomBytes(int count)
{
    if (count <= 0 || !init()) {
        return {};
    }
    QByteArray out(count, Qt::Uninitialized);
    randombytes_buf(out.data(), static_cast<std::size_t>(count));
    return out;
}

void Sodium::memzero(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        sodium_memzero(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }
}

void Sodium::memzero(void *data, std::size_t size)
{
    if (data != nullptr && size > 0) {
        sodium_memzero(data, size);
    }
}

bool Sodium::constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size() || a.isEmpty()) {
        return false;
    }
    return sodium_memcmp(a.constData(), b.constData(), static_cast<std::size_t>(a.size())) == 0;
}

QByteArray Sodium::toBase64(const QByteArray &bytes)
{
    return bytes.toBase64();
}

QByteArray Sodium::fromBase64(const QByteArray &text)
{
    return QByteArray::fromBase64(text);
}

} // namespace mini::crypto
