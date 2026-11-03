#pragma once

#include "services/ContactService.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace mini {

class Avatar;
class Settings;

// Own profile: initials avatar, name, login, bio and presence. Name and bio
// are written through ContactService when it is connected and are kept in
// config/settings.ini so they survive a restart before the server answers.
class ProfilePanel : public QDialog
{
    Q_OBJECT

public:
    explicit ProfilePanel(Settings *settings, QWidget *parent = nullptr);

    void setContactService(services::ContactService *service);
    [[nodiscard]] services::ContactService *contactService() const { return m_service; }

    void setProfile(const services::SelfProfile &profile);
    [[nodiscard]] services::SelfProfile profile() const { return m_profile; }

    [[nodiscard]] Avatar *avatar() const { return m_avatar; }
    [[nodiscard]] QLineEdit *displayNameEdit() const { return m_name; }
    [[nodiscard]] QLabel *loginLabel() const { return m_login; }
    [[nodiscard]] QPlainTextEdit *bioEdit() const { return m_bio; }
    [[nodiscard]] QLabel *statusLabel() const { return m_status; }
    [[nodiscard]] QLabel *errorLabel() const { return m_error; }
    [[nodiscard]] QPushButton *saveButton() const { return m_save; }

    void save();

signals:
    void profileUpdated(const mini::services::SelfProfile &profile);

private:
    void restore();
    void persist();
    void applyProfile(const services::SelfProfile &profile);
    void refreshStatus();
    void refreshCounter();
    void showError(const QString &code);

    Settings *m_settings = nullptr;
    services::ContactService *m_service = nullptr;
    services::SelfProfile m_profile;

    Avatar *m_avatar = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_login = nullptr;
    QPlainTextEdit *m_bio = nullptr;
    QLabel *m_counter = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_save = nullptr;
};

} // namespace mini
