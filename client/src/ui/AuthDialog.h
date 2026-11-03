#pragma once

#include "core/Settings.h"
#include "services/AuthService.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace mini::services {
class CaptchaService;
} // namespace mini::services

namespace mini::net {
class TlsPin;
class Endpoint;
} // namespace mini::net

namespace mini {

// Welcome, registration, the sign.in warning screen and sign-in, plus account
// management (devices, password, reissue, deletion). Network work runs in the
// services; this class only collects input and shows typed errors.
class AuthDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AuthDialog(QWidget *parent = nullptr);
    ~AuthDialog() override;

    [[nodiscard]] QPushButton *createButton() const { return m_create; }
    [[nodiscard]] QPushButton *loginButton() const { return m_login; }

    // Exposed so the shell can offer "Manage account" later.
    void openAccountPage();
    // Used after an explicit sign-out from the tray.
    void showWelcomePage();
    [[nodiscard]] bool hasSession() const;
    // The shell reads the session and file keys after loginRequested().
    [[nodiscard]] services::AuthService *authService() const { return m_auth; }

    // The resolver updates the address/fingerprint after the startup README
    // fetch; auth and captcha must follow without a restart.
    void applyEndpoint(const net::Endpoint &endpoint);
    void applyFingerprint(const QString &fingerprint);

signals:
    // Emitted only after a successful registration / sign-in; the shell opens
    // the main window on these signals.
    void registerRequested();
    void loginRequested();

private:
    void buildWelcomePage();
    void buildRegisterPage();
    void buildCreatedPage();
    void buildLoginPage();
    void buildAccountPage();

    void showWelcome();
    void showRegister();
    void showLogin();
    void showCreated(const services::RegistrationResult &result);

    void submitRegistration();
    void submitLogin(const QString &captchaTicket = QString());
    void askCaptcha(const std::function<void(const QString &)> &accept);

    void refreshDevices();
    void changePassword();
    void reissueSignIn();
    void deleteAccount();

    void setStatus(QLabel *label, const QString &text, bool error = false);
    [[nodiscard]] QString describeError(const QString &code) const;
    void rememberDevice(const QString &login);
    [[nodiscard]] QString deviceIdFor(const QString &login) const;

    services::AuthService *m_auth = nullptr;
    services::CaptchaService *m_captcha = nullptr;
    net::TlsPin *m_pin = nullptr;
    Settings m_settings;
    // Login of the account whose registration / sign-in just succeeded.
    QString m_lastLogin;

    QStackedWidget *m_pages = nullptr;
    QPushButton *m_create = nullptr;
    QPushButton *m_login = nullptr;
    QPushButton *m_accountShortcut = nullptr;

    // Registration.
    QLineEdit *m_regLogin = nullptr;
    QLineEdit *m_regName = nullptr;
    QLineEdit *m_regPassword = nullptr;
    QLabel *m_regStrength = nullptr;
    QLabel *m_regStatus = nullptr;
    QPushButton *m_regSubmit = nullptr;

    // Created sign.in.
    QPlainTextEdit *m_createdContent = nullptr;
    QLabel *m_createdPath = nullptr;
    QLabel *m_createdStatus = nullptr;
    QCheckBox *m_createdConfirm = nullptr;
    QPushButton *m_createdDone = nullptr;

    // Login.
    QLineEdit *m_loginLogin = nullptr;
    QLineEdit *m_loginPassword = nullptr;
    QLineEdit *m_loginFile = nullptr;
    QLabel *m_loginStatus = nullptr;
    QPushButton *m_loginSubmit = nullptr;

    // Account.
    QListWidget *m_devices = nullptr;
    QLabel *m_accountStatus = nullptr;
    QLineEdit *m_oldPassword = nullptr;
    QLineEdit *m_newPassword = nullptr;
    QLineEdit *m_deleteLogin = nullptr;
    QLineEdit *m_deletePassword = nullptr;
    QCheckBox *m_deleteConfirm = nullptr;
    QPushButton *m_accountContinue = nullptr;
};

} // namespace mini
