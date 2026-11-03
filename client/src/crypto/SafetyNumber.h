#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

#include <array>

namespace mini::crypto {

// 12 groups of 5 decimal digits, plus the same digits as one 60-character
// string and the display form ("12345 67890 ...").
struct SafetyNumberCode
{
    static constexpr int kGroupCount = 12;
    static constexpr int kGroupDigits = 5;

    std::array<quint32, kGroupCount> groups{};
    QString digits;
    QString formatted;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool operator==(const SafetyNumberCode &other) const;
    [[nodiscard]] bool operator!=(const SafetyNumberCode &other) const
    {
        return !(*this == other);
    }
};

// Safety Number per spec §4.5/§29.13:
//   SHA-256("MINI-SafetyNumber/v1" || min(uid) || max(uid) || IK_low || IK_high)
// then 12 five-digit groups derived from the digest. Users are ordered by
// login (ties by identity key), so both sides always compute the same value.
// The identity keys are Ed25519 public keys; a change in either one changes
// every group, which is what drives the "key changed" banner.
class SafetyNumber
{
public:
    SafetyNumber() = delete;

    [[nodiscard]] static Result<SafetyNumberCode> compute(const QString &userA,
                                                          const QString &userB,
                                                          const QByteArray &identityA,
                                                          const QByteArray &identityB);

    // True when a previously stored peer identity no longer matches the
    // current one. An empty stored value means "first contact", not a change.
    [[nodiscard]] static bool identityChanged(const QByteArray &stored,
                                              const QByteArray &current);
};

} // namespace mini::crypto
