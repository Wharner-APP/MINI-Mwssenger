#pragma once

#include "services/PrivacyService.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QListWidget;
class QPushButton;

namespace mini {

// Sessions and devices (§11.12 #182, §28.16): the current device is marked,
// every other one can be ended, and one button ends all other sessions.
class DevicesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DevicesDialog(services::PrivacyService *service, QWidget *parent = nullptr);

    void reload();

    [[nodiscard]] QListWidget *deviceList() const { return m_list; }
    [[nodiscard]] QPushButton *revokeButton() const { return m_revoke; }
    [[nodiscard]] QPushButton *revokeAllButton() const { return m_revokeAll; }
    [[nodiscard]] QLabel *statusLabel() const { return m_status; }
    [[nodiscard]] const QVector<services::DeviceEntry> &devices() const { return m_devices; }

private:
    void revokeSelected();
    void revokeAll();

    services::PrivacyService *m_service = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_revoke = nullptr;
    QPushButton *m_revokeAll = nullptr;
    QLabel *m_status = nullptr;
    QVector<services::DeviceEntry> m_devices;
};

} // namespace mini
