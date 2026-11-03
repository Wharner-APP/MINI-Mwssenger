#pragma once

#include <QString>

namespace mini {

// Reads config/connect.ip, config/captcha.cfg and config/server_fingerprint.txt.
// All values are optional and fall back to safe defaults.
class Config
{
public:
    Config();

    void reload();

    [[nodiscard]] QString serverEndpoint() const { return m_endpoint; }
    [[nodiscard]] QString captchaSiteKey() const { return m_captchaSiteKey; }
    [[nodiscard]] QString serverFingerprint() const { return m_fingerprint; }

private:
    QString m_endpoint;
    QString m_captchaSiteKey;
    QString m_fingerprint;
};

} // namespace mini
