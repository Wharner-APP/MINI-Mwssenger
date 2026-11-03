#include "crypto/SafetyNumber.h"

#include "crypto/Sodium.h"

#include <QStringList>

#include <sodium.h>

namespace mini::crypto {

namespace {

constexpr int kIdentityBytes = crypto_sign_PUBLICKEYBYTES;

QByteArray sha256(const QByteArray &input)
{
    QByteArray digest(crypto_hash_sha256_BYTES, Qt::Uninitialized);
    crypto_hash_sha256(reinterpret_cast<unsigned char *>(digest.data()),
                       reinterpret_cast<const unsigned char *>(input.constData()),
                       static_cast<unsigned long long>(input.size()));
    return digest;
}

void appendLengthPrefixed(QByteArray &out, const QByteArray &value)
{
    const quint16 length = static_cast<quint16>(qMin<qsizetype>(value.size(), 0xffff));
    out.append(static_cast<char>(length & 0xff));
    out.append(static_cast<char>((length >> 8) & 0xff));
    out.append(value.constData(), length);
}

quint32 readBigEndian32(const char *p)
{
    const auto *raw = reinterpret_cast<const unsigned char *>(p);
    return (static_cast<quint32>(raw[0]) << 24) | (static_cast<quint32>(raw[1]) << 16)
        | (static_cast<quint32>(raw[2]) << 8) | static_cast<quint32>(raw[3]);
}

} // namespace

bool SafetyNumberCode::isValid() const
{
    return digits.size() == kGroupCount * kGroupDigits && !formatted.isEmpty();
}

bool SafetyNumberCode::operator==(const SafetyNumberCode &other) const
{
    return groups == other.groups && digits == other.digits;
}

Result<SafetyNumberCode> SafetyNumber::compute(const QString &userA,
                                               const QString &userB,
                                               const QByteArray &identityA,
                                               const QByteArray &identityB)
{
    if (!Sodium::init()) {
        return Result<SafetyNumberCode>::error(QStringLiteral("crypto.init"));
    }
    if (identityA.size() != kIdentityBytes || identityB.size() != kIdentityBytes) {
        return Result<SafetyNumberCode>::error(QStringLiteral("crypto.safety_identity"));
    }

    const QByteArray loginA = userA.toUtf8();
    const QByteArray loginB = userB.toUtf8();
    int order = QString::compare(userA, userB, Qt::CaseSensitive);
    if (order == 0) {
        order = identityA.compare(identityB) <= 0 ? -1 : 1;
    }
    const QByteArray &lowLogin = order <= 0 ? loginA : loginB;
    const QByteArray &highLogin = order <= 0 ? loginB : loginA;
    const QByteArray &lowIdentity = order <= 0 ? identityA : identityB;
    const QByteArray &highIdentity = order <= 0 ? identityB : identityA;

    QByteArray input = QByteArrayLiteral("MINI-SafetyNumber/v1");
    appendLengthPrefixed(input, lowLogin);
    appendLengthPrefixed(input, highLogin);
    input.append(lowIdentity);
    input.append(highIdentity);
    QByteArray digest = sha256(input);
    Sodium::memzero(input);

    SafetyNumberCode code;
    int produced = 0;
    for (char round = 1; produced < SafetyNumberCode::kGroupCount; ++round) {
        QByteArray roundInput = digest;
        roundInput.append(round);
        const QByteArray block = sha256(roundInput);
        Sodium::memzero(roundInput);
        for (int offset = 0;
             offset + 4 <= block.size() && produced < SafetyNumberCode::kGroupCount;
             offset += 4) {
            code.groups[static_cast<std::size_t>(produced)] =
                readBigEndian32(block.constData() + offset) % 100000u;
            ++produced;
        }
    }
    Sodium::memzero(digest);

    QStringList formatted;
    for (const quint32 group : code.groups) {
        const QString text = QStringLiteral("%1").arg(group, SafetyNumberCode::kGroupDigits,
                                                      10, QLatin1Char('0'));
        code.digits.append(text);
        formatted.append(text);
    }
    code.formatted = formatted.join(QLatin1Char(' '));
    return Result<SafetyNumberCode>::ok(std::move(code));
}

bool SafetyNumber::identityChanged(const QByteArray &stored, const QByteArray &current)
{
    if (stored.isEmpty()) {
        return false;
    }
    return !Sodium::constantTimeEquals(stored, current);
}

} // namespace mini::crypto
