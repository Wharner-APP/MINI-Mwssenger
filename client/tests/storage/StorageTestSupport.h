#pragma once

#include <QByteArray>

namespace mini::test {

// 32-byte DB_KEY used only by tests; production keys come from Kdf.
inline QByteArray dbKey()
{
    return QByteArray(32, '\x5a');
}

} // namespace mini::test
