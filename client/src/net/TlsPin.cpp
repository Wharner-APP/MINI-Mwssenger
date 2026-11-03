#include "net/TlsPin.h"

#include <QCryptographicHash>
#include <QRegularExpression>

namespace mini::net {

namespace {

constexpr int kSha256HexLength = 64;

bool isHex(const QString &value)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9A-F]*$"));
    return hex.match(value).hasMatch();
}

} // namespace

TlsPin::TlsPin(QString expectedSha256, QObject *parent)
    : QObject(parent)
{
    setExpected(expectedSha256);
}

QString TlsPin::normalize(const QString &fingerprint)
{
    QString value = fingerprint.trimmed();
    if (value.startsWith(QStringLiteral("sha256="), Qt::CaseInsensitive)) {
        value = value.mid(7);
    } else if (value.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        value = value.mid(7);
    }
    value.remove(QLatin1Char(':'));
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value = value.toUpper();
    if (value.size() != kSha256HexLength || !isHex(value)) {
        return {};
    }
    return value;
}

bool TlsPin::isValid(const QString &fingerprint)
{
    return !normalize(fingerprint).isEmpty();
}

QString TlsPin::fingerprint(const QSslCertificate &certificate)
{
    if (certificate.isNull()) {
        return {};
    }
    return QString::fromLatin1(
        certificate.digest(QCryptographicHash::Sha256).toHex().toUpper());
}

TlsPin::Outcome TlsPin::check(const QSslCertificate &certificate) const
{
    if (m_invalid) {
        return Outcome::Invalid;
    }
    if (m_expected.isEmpty()) {
        return Outcome::NotConfigured;
    }
    const QString actual = fingerprint(certificate);
    if (actual.isEmpty()) {
        return Outcome::Invalid;
    }
    return actual == m_expected ? Outcome::Match : Outcome::Mismatch;
}

bool TlsPin::verify(const QSslCertificate &certificate) const
{
    return check(certificate) == Outcome::Match;
}

void TlsPin::setExpected(const QString &fingerprint)
{
    m_rawExpected = fingerprint.trimmed();
    const QString normalized = normalize(fingerprint);
    m_expected = normalized;
    m_invalid = !m_rawExpected.isEmpty() && normalized.isEmpty();
    m_acceptOnce = false;
    m_warnedUnavailable = false;
}

void TlsPin::acceptMismatchOnce()
{
    m_acceptOnce = true;
}

bool TlsPin::shouldIgnore(const QSslCertificate &certificate)
{
    const Outcome outcome = check(certificate);
    if (outcome == Outcome::Match) {
        return true;
    }
    if (outcome == Outcome::Mismatch) {
        if (m_acceptOnce) {
            m_acceptOnce = false;
            return true;
        }
        emit mismatchDetected(fingerprint(certificate), m_expected);
        return false;
    }

    if (!m_warnedUnavailable) {
        m_warnedUnavailable = true;
        QString reason = QStringLiteral("not_configured");
        if (m_invalid) {
            reason = QStringLiteral("invalid_fingerprint");
        } else if (certificate.isNull() && !m_expected.isEmpty()) {
            reason = QStringLiteral("no_certificate");
        }
        emit pinUnavailable(reason, m_rawExpected);
    }
    return false;
}

} // namespace mini::net
