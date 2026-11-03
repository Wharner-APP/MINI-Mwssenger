#include "ui/AuthDialog.h"

#include "app/Config.h"
#include "app/Paths.h"
#include "net/TlsPin.h"
#include "net/TunnelResolver.h"
#include "services/CaptchaService.h"
#include "ui/CaptchaDialog.h"
#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace mini {

namespace {

QWidget *page(QStackedWidget *stack)
{
    auto *widget = new QWidget(stack);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    stack->addWidget(widget);
    return widget;
}

QLineEdit *field(QWidget *parent, const QString &placeholder, bool password = false)
{
    auto *edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    edit->setMinimumHeight(36);
    if (password) {
        edit->setEchoMode(QLineEdit::Password);
    }
    return edit;
}

QPushButton *button(QWidget *parent, const QString &text, bool accent = false)
{
    auto *item = new QPushButton(text, parent);
    item->setCursor(Qt::PointingHandCursor);
    item->setMinimumHeight(36);
    if (accent) {
        item->setProperty("miniRole", QStringLiteral("accent"));
    }
    return item;
}

void addRow(QVBoxLayout *layout, QWidget *parent, const QString &label, QWidget *widget)
{
    auto *caption = new QLabel(label, parent);
    caption->setProperty("miniRole", QStringLiteral("caption"));
    layout->addWidget(caption);
    layout->addWidget(widget);
}

} // namespace

AuthDialog::AuthDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("authDialog"));
    setWindowTitle(tr("MINI Messenger"));
    setModal(false);
    setMinimumSize(520, 560);
    resize(540, 660);

    Config config;
    const auto parsed = net::TunnelResolver::parseEndpoint(config.serverEndpoint());
    const net::Endpoint endpoint = parsed.isOk()
        ? parsed.value()
        : net::Endpoint(QStringLiteral("http"), QStringLiteral("127.0.0.1"), 8765);

    m_auth = new services::AuthService(endpoint, this);
    m_captcha = new services::CaptchaService(endpoint, config.captchaSiteKey(), this);
    m_pin = new net::TlsPin(config.serverFingerprint(), this);
    m_auth->setTlsPin(m_pin);
    m_captcha->setTlsPin(m_pin);
    // Reissue rekeys data/mini.db with data/keys.bin, never data/messages.db.
    m_auth->setLocalDataPaths(
        QDir(Paths::dataDir()).filePath(QStringLiteral("mini.db")),
        QDir(Paths::dataDir()).filePath(QStringLiteral("keys.bin")));
    // Last used device id; sign-in/takeover replaces it with the value stored
    // for that specific login so two accounts on one client do not collide.
    m_auth->setDeviceId(m_settings.get(QStringLiteral("auth/device_id")));
    connect(m_auth, &services::AuthService::authenticationRequired, this, [this] {
        m_auth->clearSession();
        showLogin();
        setStatus(m_loginStatus, tr("Session expired. Sign in again."), true);
    });

    auto *logo = new QLabel(this);
    logo->setPixmap(Icons::pixmap(QStringLiteral("app"), QColor(), 48));
    logo->setFixedSize(48, 48);
    logo->setScaledContents(true);

    auto *title = new QLabel(tr("MINI Messenger"), this);
    title->setProperty("miniRole", QStringLiteral("title"));
    title->setAlignment(Qt::AlignCenter);

    m_pages = new QStackedWidget(this);

    buildWelcomePage();
    buildRegisterPage();
    buildCreatedPage();
    buildLoginPage();
    buildAccountPage();

    m_accountShortcut = button(this, tr("Manage account"));
    m_accountShortcut->setVisible(false);
    connect(m_accountShortcut, &QPushButton::clicked, this, &AuthDialog::openAccountPage);

    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    footer->addWidget(m_accountShortcut);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 28, 40, 24);
    layout->setSpacing(10);
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(m_pages, 1);
    layout->addLayout(footer);

    showWelcome();
}

AuthDialog::~AuthDialog() = default;

void AuthDialog::applyEndpoint(const net::Endpoint &endpoint)
{
    if (m_auth) {
        m_auth->setEndpoint(endpoint);
    }
    if (m_captcha) {
        m_captcha->setEndpoint(endpoint);
    }
}

void AuthDialog::applyFingerprint(const QString &fingerprint)
{
    if (m_pin && !fingerprint.isEmpty()) {
        m_pin->setExpected(fingerprint);
    }
}

bool AuthDialog::hasSession() const
{
    return m_auth && m_auth->hasSession();
}

void AuthDialog::buildWelcomePage()
{
    QWidget *widget = page(m_pages);
    auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());

    auto *subtitle = new QLabel(tr("A quiet messenger for people you know."), widget);
    subtitle->setProperty("miniRole", QStringLiteral("subtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);

    m_create = button(widget, tr("Create account"), true);
    m_login = button(widget, tr("Log in"));
    auto *note = new QLabel(tr("No phone number or email. A sign.in file protects your account."),
                            widget);
    note->setProperty("miniRole", QStringLiteral("caption"));
    note->setAlignment(Qt::AlignCenter);
    note->setWordWrap(true);

    layout->addWidget(subtitle);
    layout->addStretch(1);
    layout->addWidget(m_create);
    layout->addWidget(m_login);
    layout->addSpacing(6);
    layout->addWidget(note);

    connect(m_create, &QPushButton::clicked, this, &AuthDialog::showRegister);
    connect(m_login, &QPushButton::clicked, this, &AuthDialog::showLogin);
}

void AuthDialog::buildRegisterPage()
{
    QWidget *widget = page(m_pages);
    auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());

    auto *title = new QLabel(tr("Create account"), widget);
    title->setProperty("miniRole", QStringLiteral("heading"));

    m_regLogin = field(widget, tr("Login (4–24 letters, digits, underscore)"));
    m_regName = field(widget, tr("Display name"));
    m_regPassword = field(widget, tr("Password (8–128 characters)"), true);
    m_regStrength = new QLabel(widget);
    m_regStrength->setProperty("miniRole", QStringLiteral("caption"));
    m_regStatus = new QLabel(widget);
    m_regStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_regStatus->setWordWrap(true);

    m_regSubmit = button(widget, tr("Create account"), true);
    auto *back = button(widget, tr("Back"));

    addRow(layout, widget, tr("Login"), m_regLogin);
    addRow(layout, widget, tr("Name"), m_regName);
    addRow(layout, widget, tr("Password"), m_regPassword);
    layout->addWidget(m_regStrength);
    layout->addWidget(m_regStatus);
    layout->addStretch(1);
    layout->addWidget(m_regSubmit);
    layout->addWidget(back);

    connect(m_regPassword, &QLineEdit::textChanged, this, [this](const QString &text) {
        QString level;
        switch (services::AuthService::passwordScore(text)) {
        case 0: level = tr("too weak"); break;
        case 1: level = tr("weak"); break;
        case 2: level = tr("fair"); break;
        case 3: level = tr("good"); break;
        default: level = tr("strong"); break;
        }
        m_regStrength->setText(tr("Password strength: %1").arg(level));
    });
    connect(m_regSubmit, &QPushButton::clicked, this, &AuthDialog::submitRegistration);
    connect(back, &QPushButton::clicked, this, &AuthDialog::showWelcome);
}

void AuthDialog::buildCreatedPage()
{
    QWidget *widget = page(m_pages);
    auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());

    auto *title = new QLabel(tr("Your sign.in file is ready"), widget);
    title->setProperty("miniRole", QStringLiteral("heading"));

    auto *warning = new QLabel(
        tr("Without this file you cannot sign in, and it cannot be restored. "
           "Keep it safe and never edit its contents."),
        widget);
    warning->setWordWrap(true);
    warning->setProperty("miniRole", QStringLiteral("danger"));

    m_createdPath = new QLabel(widget);
    m_createdPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_createdPath->setWordWrap(true);
    m_createdPath->setProperty("miniRole", QStringLiteral("caption"));

    m_createdContent = new QPlainTextEdit(widget);
    m_createdContent->setReadOnly(true);
    m_createdContent->setMinimumHeight(110);
    m_createdContent->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *copy = button(widget, tr("Copy contents"));
    m_createdConfirm = new QCheckBox(
        tr("I saved the file and understand I cannot sign in without it."), widget);
    m_createdStatus = new QLabel(widget);
    m_createdStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_createdStatus->setWordWrap(true);
    m_createdDone = button(widget, tr("Done"), true);
    m_createdDone->setEnabled(false);

    layout->addWidget(title);
    layout->addWidget(warning);
    layout->addWidget(m_createdPath);
    layout->addWidget(m_createdContent, 1);
    layout->addWidget(copy);
    layout->addWidget(m_createdConfirm);
    layout->addWidget(m_createdStatus);
    layout->addWidget(m_createdDone);

    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_createdContent->toPlainText());
        setStatus(m_createdStatus, tr("Copied to clipboard."));
    });
    connect(m_createdConfirm, &QCheckBox::toggled, m_createdDone, &QPushButton::setEnabled);
    connect(m_createdDone, &QPushButton::clicked, this, [this] {
        rememberDevice(m_lastLogin);
        emit registerRequested();
    });
}

void AuthDialog::buildLoginPage()
{
    QWidget *widget = page(m_pages);
    auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());

    auto *title = new QLabel(tr("Log in"), widget);
    title->setProperty("miniRole", QStringLiteral("heading"));

    m_loginLogin = field(widget, tr("Login"));
    m_loginPassword = field(widget, tr("Password"), true);
    m_loginFile = field(widget, tr("Path to sign.in"));
    m_loginFile->setText(m_settings.get(QStringLiteral("auth/last_signin_path")));
    auto *browse = button(widget, tr("Choose file…"));
    auto *fileRow = new QHBoxLayout();
    fileRow->addWidget(m_loginFile, 1);
    fileRow->addWidget(browse);

    m_loginStatus = new QLabel(widget);
    m_loginStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_loginStatus->setWordWrap(true);
    m_loginSubmit = button(widget, tr("Log in"), true);
    auto *back = button(widget, tr("Back"));

    addRow(layout, widget, tr("Login"), m_loginLogin);
    addRow(layout, widget, tr("Password"), m_loginPassword);
    auto *caption = new QLabel(tr("sign.in file"), widget);
    caption->setProperty("miniRole", QStringLiteral("caption"));
    layout->addWidget(caption);
    layout->addLayout(fileRow);
    layout->addWidget(m_loginStatus);
    layout->addStretch(1);
    layout->addWidget(m_loginSubmit);
    layout->addWidget(back);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose sign.in"), m_loginFile->text(),
            tr("MINI sign-in (*.in);;All files (*)"));
        if (!path.isEmpty()) {
            m_loginFile->setText(path);
        }
    });
    connect(m_loginSubmit, &QPushButton::clicked, this, [this] { submitLogin(); });
    connect(m_loginPassword, &QLineEdit::returnPressed, this, [this] { submitLogin(); });
    connect(back, &QPushButton::clicked, this, &AuthDialog::showWelcome);
}

void AuthDialog::buildAccountPage()
{
    QWidget *widget = page(m_pages);
    auto *layout = qobject_cast<QVBoxLayout *>(widget->layout());

    auto *title = new QLabel(tr("Account"), widget);
    title->setProperty("miniRole", QStringLiteral("heading"));

    auto *devicesCaption = new QLabel(tr("Active devices"), widget);
    devicesCaption->setProperty("miniRole", QStringLiteral("caption"));
    m_devices = new QListWidget(widget);
    m_devices->setMinimumHeight(80);
    auto *devicesRow = new QHBoxLayout();
    auto *refresh = button(widget, tr("Refresh"));
    auto *endSession = button(widget, tr("End session"));
    devicesRow->addWidget(refresh);
    devicesRow->addWidget(endSession);

    auto *passwordCaption = new QLabel(tr("Change password"), widget);
    passwordCaption->setProperty("miniRole", QStringLiteral("caption"));
    m_oldPassword = field(widget, tr("Current password"), true);
    m_newPassword = field(widget, tr("New password"), true);
    auto *change = button(widget, tr("Change password"));

    auto *reissue = button(widget, tr("Create a new sign.in"));
    auto *deleteCaption = new QLabel(tr("Delete account"), widget);
    deleteCaption->setProperty("miniRole", QStringLiteral("danger"));
    m_deleteLogin = field(widget, tr("Type your login"));
    m_deletePassword = field(widget, tr("Password"), true);
    m_deleteConfirm = new QCheckBox(tr("I understand this cannot be undone."), widget);
    auto *deleteButton = button(widget, tr("Delete account"));
    auto *signOut = button(widget, tr("Sign out"));

    m_accountStatus = new QLabel(widget);
    m_accountStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_accountStatus->setWordWrap(true);
    m_accountContinue = button(widget, tr("Continue to chats"), true);
    m_accountContinue->setVisible(false);
    auto *back = button(widget, tr("Back"));

    layout->addWidget(title);
    layout->addWidget(devicesCaption);
    layout->addWidget(m_devices);
    layout->addLayout(devicesRow);
    layout->addWidget(passwordCaption);
    layout->addWidget(m_oldPassword);
    layout->addWidget(m_newPassword);
    layout->addWidget(change);
    layout->addWidget(reissue);
    layout->addWidget(deleteCaption);
    layout->addWidget(m_deleteLogin);
    layout->addWidget(m_deletePassword);
    layout->addWidget(m_deleteConfirm);
    layout->addWidget(deleteButton);
    layout->addWidget(signOut);
    layout->addWidget(m_accountStatus);
    layout->addStretch(1);
    layout->addWidget(m_accountContinue);
    layout->addWidget(back);

    connect(refresh, &QPushButton::clicked, this, &AuthDialog::refreshDevices);
    connect(endSession, &QPushButton::clicked, this, [this] {
        const auto *item = m_devices->currentItem();
        if (!item) {
            setStatus(m_accountStatus, tr("Choose a device first."), true);
            return;
        }
        const QString deviceId = item->data(Qt::UserRole).toString();
        m_auth->revokeDevice(deviceId, [this](Result<bool> result) {
            if (result.isError()) {
                setStatus(m_accountStatus, describeError(result.error()), true);
                return;
            }
            setStatus(m_accountStatus, tr("Session ended."));
            refreshDevices();
        });
    });
    connect(change, &QPushButton::clicked, this, &AuthDialog::changePassword);
    connect(reissue, &QPushButton::clicked, this, &AuthDialog::reissueSignIn);
    connect(deleteButton, &QPushButton::clicked, this, &AuthDialog::deleteAccount);
    connect(signOut, &QPushButton::clicked, this, [this] {
        setStatus(m_accountStatus, tr("Signing out…"));
        m_auth->logout([this](Result<bool> result) {
            m_accountShortcut->setVisible(false);
            m_accountContinue->setVisible(false);
            showWelcome();
            if (result.isError()) {
                setStatus(m_loginStatus, describeError(result.error()), true);
            }
        });
    });
    connect(m_accountContinue, &QPushButton::clicked, this, [this] { emit loginRequested(); });
    connect(back, &QPushButton::clicked, this, &AuthDialog::showWelcome);
}

void AuthDialog::showWelcome()
{
    m_pages->setCurrentIndex(0);
    m_accountShortcut->setVisible(hasSession());
    setStatus(m_regStatus, QString());
    setStatus(m_loginStatus, QString());
}

void AuthDialog::showRegister()
{
    m_pages->setCurrentIndex(1);
    m_regStatus->clear();
    m_regSubmit->setEnabled(true);
    m_regLogin->setFocus();
}

void AuthDialog::showLogin()
{
    m_pages->setCurrentIndex(3);
    m_loginStatus->clear();
    m_loginSubmit->setEnabled(true);
    m_loginFile->setText(m_settings.get(QStringLiteral("auth/last_signin_path"),
                                        m_loginFile->text()));
    m_loginLogin->setFocus();
}

void AuthDialog::showCreated(const services::RegistrationResult &result)
{
    m_createdPath->setText(tr("Saved to: %1").arg(result.filePath));
    m_createdContent->setPlainText(QString::fromUtf8(result.material.fileBytes));
    m_createdConfirm->setChecked(false);
    m_createdDone->setEnabled(false);
    if (result.usedFallbackDirectory) {
        setStatus(m_createdStatus,
                  tr("Downloads is unavailable. The file was saved in the app data folder — "
                     "move it somewhere safe."),
                  true);
    } else {
        setStatus(m_createdStatus, QString());
    }
    m_pages->setCurrentIndex(2);
}

void AuthDialog::submitRegistration()
{
    const QString login = m_regLogin->text().trimmed();
    const QString name = m_regName->text().trimmed();
    const QString password = m_regPassword->text();

    if (auto result = services::AuthService::validateLogin(login); result.isError()) {
        setStatus(m_regStatus, describeError(result.error()), true);
        return;
    }
    if (auto result = services::AuthService::validateDisplayName(name); result.isError()) {
        setStatus(m_regStatus, describeError(result.error()), true);
        return;
    }
    if (auto result = services::AuthService::validatePassword(password, login, name);
        result.isError()) {
        setStatus(m_regStatus, describeError(result.error()), true);
        return;
    }

    // Availability is checked before any captcha is shown (story 26).
    m_regSubmit->setEnabled(false);
    setStatus(m_regStatus, tr("Checking login…"));
    m_auth->checkLogin(login, [this, login, name, password](
                                 Result<services::LoginAvailability> availability) {
        if (availability.isError()) {
            m_regSubmit->setEnabled(true);
            setStatus(m_regStatus, describeError(availability.error()), true);
            return;
        }
        if (!availability.value().available) {
            m_regSubmit->setEnabled(true);
            const QString reason = availability.value().reason.isEmpty()
                ? QStringLiteral("taken")
                : availability.value().reason;
            setStatus(m_regStatus, describeError(QStringLiteral("login.%1").arg(reason)), true);
            return;
        }

        auto run = [this, login, name, password](const QString &ticket) {
            m_regSubmit->setEnabled(false);
            setStatus(m_regStatus, tr("Creating account…"));
            m_auth->registerAccount(login, name, password, ticket,
                                    [this, login](Result<services::RegistrationResult> result) {
                m_regSubmit->setEnabled(true);
                if (result.isError()) {
                    setStatus(m_regStatus, describeError(result.error()), true);
                    return;
                }
                m_lastLogin = login;
                rememberDevice(login);
                showCreated(result.value());
            });
        };

        if (m_captcha->required()) {
            askCaptcha(run);
        } else {
            run(QString());
        }
    });
}

void AuthDialog::submitLogin(const QString &captchaTicket)
{
    const QString login = m_loginLogin->text().trimmed();
    const QString password = m_loginPassword->text();
    const QString path = m_loginFile->text().trimmed();
    if (login.isEmpty() || password.isEmpty()) {
        setStatus(m_loginStatus, tr("Enter your login and password."), true);
        return;
    }
    if (path.isEmpty()) {
        setStatus(m_loginStatus, tr("Choose your sign.in file."), true);
        return;
    }

    m_loginSubmit->setEnabled(false);
    setStatus(m_loginStatus, tr("Signing in…"));
    // Use the device registered for this exact account when we know it; the
    // last-used id may belong to another account on the same client.
    const QString knownDevice = deviceIdFor(login);
    if (!knownDevice.isEmpty()) {
        m_auth->setDeviceId(knownDevice);
    }
    m_auth->login(login, password, path, captchaTicket,
                  [this, path, captchaTicket](Result<services::AuthSession> result) {
        m_loginSubmit->setEnabled(true);
        if (result.isError()) {
            if (result.error() == QStringLiteral("captcha_required") && captchaTicket.isEmpty()) {
                askCaptcha([this](const QString &ticket) { submitLogin(ticket); });
                return;
            }
            setStatus(m_loginStatus, describeError(result.error()), true);
            return;
        }
        m_settings.set(QStringLiteral("auth/last_signin_path"), path);
        m_lastLogin = result.value().login;
        rememberDevice(result.value().login);
        setStatus(m_loginStatus, QString());
        openAccountPage();
        setStatus(m_accountStatus, tr("Signed in as %1.").arg(result.value().login));
    });
}

void AuthDialog::askCaptcha(const std::function<void(const QString &)> &accept)
{
    auto *dialog = new CaptchaDialog(m_captcha, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &CaptchaDialog::tokenReady, this, [accept](const QString &ticket) {
        accept(ticket);
    });
    connect(dialog, &QDialog::finished, this, [this](int code) {
        if (code != QDialog::Accepted) {
            m_regSubmit->setEnabled(true);
            m_loginSubmit->setEnabled(true);
        }
    });
    dialog->open();
}

void AuthDialog::refreshDevices()
{
    if (!hasSession()) {
        setStatus(m_accountStatus, tr("Sign in first."), true);
        return;
    }
    setStatus(m_accountStatus, tr("Loading devices…"));
    m_auth->listDevices([this](Result<QList<services::AuthDevice>> result) {
        if (result.isError()) {
            setStatus(m_accountStatus, describeError(result.error()), true);
            return;
        }
        m_devices->clear();
        for (const services::AuthDevice &device : result.value()) {
            auto *item = new QListWidgetItem(
                tr("%1 — %2%3").arg(device.name.isEmpty() ? tr("Unknown device") : device.name,
                                    device.platform.isEmpty() ? QStringLiteral("—")
                                                              : device.platform,
                                    device.current ? tr(" (this device)") : QString()),
                m_devices);
            item->setData(Qt::UserRole, device.deviceId);
        }
        setStatus(m_accountStatus, QString());
    });
}

void AuthDialog::changePassword()
{
    if (!hasSession()) {
        setStatus(m_accountStatus, tr("Sign in first."), true);
        return;
    }
    const QString oldPassword = m_oldPassword->text();
    const QString newPassword = m_newPassword->text();
    if (oldPassword.isEmpty() || newPassword.isEmpty()) {
        setStatus(m_accountStatus, tr("Enter both passwords."), true);
        return;
    }
    setStatus(m_accountStatus, tr("Changing password…"));
    m_auth->changePassword(oldPassword, newPassword, [this](Result<bool> result) {
        if (result.isError()) {
            setStatus(m_accountStatus, describeError(result.error()), true);
            return;
        }
        m_oldPassword->clear();
        m_newPassword->clear();
        setStatus(m_accountStatus, tr("Password changed. Other sessions were ended."));
    });
}

void AuthDialog::reissueSignIn()
{
    if (!hasSession()) {
        setStatus(m_accountStatus, tr("Sign in first."), true);
        return;
    }
    setStatus(m_accountStatus, tr("Creating a new sign.in…"));
    m_auth->reissueSignIn([this](Result<services::ReissueResult> result) {
        if (result.isError()) {
            setStatus(m_accountStatus, describeError(result.error()), true);
            return;
        }
        m_settings.set(QStringLiteral("auth/last_signin_path"), result.value().filePath);
        m_settings.flush();
        setStatus(m_accountStatus,
                  tr("New sign.in saved to: %1. The old file no longer works.")
                      .arg(result.value().filePath));
    });
}

void AuthDialog::deleteAccount()
{
    if (!hasSession()) {
        setStatus(m_accountStatus, tr("Sign in first."), true);
        return;
    }
    if (!m_deleteConfirm->isChecked()) {
        setStatus(m_accountStatus, tr("Confirm that you understand this cannot be undone."), true);
        return;
    }
    setStatus(m_accountStatus, tr("Deleting account…"));
    m_auth->deleteAccount(m_deleteLogin->text(), m_deletePassword->text(), true,
                          [this](Result<bool> result) {
        if (result.isError()) {
            setStatus(m_accountStatus, describeError(result.error()), true);
            return;
        }
        m_deleteLogin->clear();
        m_deletePassword->clear();
        m_deleteConfirm->setChecked(false);
        m_accountShortcut->setVisible(false);
        showWelcome();
    });
}

void AuthDialog::showWelcomePage()
{
    showWelcome();
    show();
    raise();
    activateWindow();
}

void AuthDialog::openAccountPage()
{
    m_pages->setCurrentIndex(4);
    m_accountStatus->clear();
    m_accountContinue->setVisible(hasSession());
    m_accountShortcut->setVisible(hasSession());
    refreshDevices();
}

void AuthDialog::setStatus(QLabel *label, const QString &text, bool error)
{
    if (!label) {
        return;
    }
    label->setText(text);
    label->setProperty("miniRole", error ? QStringLiteral("danger")
                                         : QStringLiteral("caption"));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QString AuthDialog::describeError(const QString &code) const
{
    if (code == QStringLiteral("login.invalid_format")) {
        return tr("Login: 4–24 characters, latin letters, digits and underscore; starts with a letter.");
    }
    if (code == QStringLiteral("login.required")) {
        return tr("Enter your login.");
    }
    if (code == QStringLiteral("login.reserved")) {
        return tr("This login is reserved.");
    }
    if (code == QStringLiteral("login.taken") || code == QStringLiteral("login_taken")) {
        return tr("This login is already taken.");
    }
    if (code == QStringLiteral("name.invalid")) {
        return tr("Name must be 1–64 characters without control characters.");
    }
    if (code == QStringLiteral("password.length")) {
        return tr("Password must be 8–128 characters.");
    }
    if (code == QStringLiteral("password.composition")) {
        return tr("Password must contain at least one letter and one digit.");
    }
    if (code == QStringLiteral("password.equals_login")) {
        return tr("Password must not be the same as the login.");
    }
    if (code == QStringLiteral("password.equals_name")) {
        return tr("Password must not be the same as the name.");
    }
    if (code == QStringLiteral("password.common")) {
        return tr("This password is too common.");
    }
    if (code == QStringLiteral("password.required")) {
        return tr("Enter your password.");
    }
    if (code == QStringLiteral("signin.missing")) {
        return tr("Choose your sign.in file.");
    }
    if (code.startsWith(QStringLiteral("signin."))) {
        return tr("This sign.in file is damaged or was edited. Use the original file.");
    }
    if (code == QStringLiteral("captcha.required")) {
        return tr("Complete the captcha.");
    }
    if (code == QStringLiteral("captcha_invalid") || code == QStringLiteral("captcha_expired")) {
        return tr("The captcha is wrong or expired. Try again.");
    }
    if (code == QStringLiteral("captcha_blocked") || code == QStringLiteral("account_locked")
        || code == QStringLiteral("rate_limited")) {
        return tr("Too many attempts. Wait a little and try again.");
    }
    if (code == QStringLiteral("invalid_credentials")) {
        return tr("Wrong login, password or sign.in file.");
    }
    if (code == QStringLiteral("device_limit")) {
        return tr("Another device is already signed in. End that session first.");
    }
    if (code == QStringLiteral("nonce_expired") || code == QStringLiteral("nonce_invalid")
        || code == QStringLiteral("nonce_reused")) {
        return tr("The sign-in attempt expired. Try again.");
    }
    if (code == QStringLiteral("unauthorized") || code == QStringLiteral("session_revoked")) {
        return tr("Session expired. Sign in again.");
    }
    if (code == QStringLiteral("network") || code == QStringLiteral("timeout")) {
        return tr("The server is unreachable. Check your connection and try again.");
    }
    if (code == QStringLiteral("tls_required") || code == QStringLiteral("tls_pin_required")) {
        return tr("A secure connection is required. Set an https address and the server fingerprint.");
    }
    if (code == QStringLiteral("tls_fingerprint")) {
        return tr("The server certificate does not match the pinned fingerprint.");
    }
    if (code == QStringLiteral("rekey.rollback")) {
        return tr("Could not roll back local data. Check that your old sign.in file still works.");
    }
    if (code == QStringLiteral("reissue.rekey") || code.startsWith(QStringLiteral("rekey."))) {
        return tr("Could not re-encrypt the local database. The old sign.in is still valid.");
    }
    if (code == QStringLiteral("delete.unconfirmed")) {
        return tr("Confirm that you understand this cannot be undone.");
    }
    if (code == QStringLiteral("delete.login_mismatch")) {
        return tr("The typed login does not match this account.");
    }
    return tr("Something went wrong (%1).").arg(code);
}

QString AuthDialog::deviceIdFor(const QString &login) const
{
    const QString key = QStringLiteral("auth/device_id/") + login.trimmed().toLower();
    return m_settings.get(key);
}

void AuthDialog::rememberDevice(const QString &login)
{
    if (!m_auth) {
        return;
    }
    const QString deviceId = m_auth->deviceId();
    m_settings.set(QStringLiteral("auth/device_id"), deviceId);
    if (!login.trimmed().isEmpty()) {
        m_settings.set(QStringLiteral("auth/device_id/") + login.trimmed().toLower(),
                       deviceId);
    }
    m_settings.flush();
}

} // namespace mini
