#pragma once

#include <QObject>
#include <QSslCertificate>
#include <QString>

namespace mini::net {

// SHA-256 certificate pinning. The expected value is the hex digest published
// next to the server address as "CURRENT FINGERPRINT".
class TlsPin : public QObject
{
    Q_OBJECT

public:
    enum class Outcome {
        Match,
        Mismatch,
        NotConfigured,
        Invalid
    };
    Q_ENUM(Outcome)

    explicit TlsPin(QString expectedSha256 = QString(), QObject *parent = nullptr);

    // Accepts "AB:CD:...", "abcd...", optional "sha256=" prefix; returns
    // uppercase hex without separators, or an empty string when invalid.
    [[nodiscard]] static QString normalize(const QString &fingerprint);
    [[nodiscard]] static bool isValid(const QString &fingerprint);
    [[nodiscard]] static QString fingerprint(const QSslCertificate &certificate);

    [[nodiscard]] Outcome check(const QSslCertificate &certificate) const;
    // True only when the certificate matches the configured pin.
    [[nodiscard]] bool verify(const QSslCertificate &certificate) const;

    [[nodiscard]] QString expected() const { return m_expected; }
    [[nodiscard]] bool isConfigured() const { return !m_expected.isEmpty(); }
    [[nodiscard]] bool isMalformed() const { return m_invalid; }
    void setExpected(const QString &fingerprint);

    // The UI calls this after the user confirms a "server replaced?" warning.
    void acceptMismatchOnce();

    // Used by the transports on QSslSocket::sslErrors. Returns true when the
    // error may be ignored (pin matched or the user accepted the mismatch).
    bool shouldIgnore(const QSslCertificate &certificate);

signals:
    void mismatchDetected(const QString &actual, const QString &expected);
    // Emitted once when no usable pin is configured. Callers must treat this
    // as a warning for HTTPS and as a hard failure for WSS.
    void pinUnavailable(const QString &reason, const QString &expected);

private:
    QString m_expected;
    QString m_rawExpected;
    bool m_invalid = false;
    bool m_acceptOnce = false;
    bool m_warnedUnavailable = false;
};

} // namespace mini::net
