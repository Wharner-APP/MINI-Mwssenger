#include "ui/DevicesDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace mini {

namespace {

QString lastSeenText(qint64 lastSeenAt)
{
    if (lastSeenAt <= 0) {
        return DevicesDialog::tr("unknown");
    }
    const qint64 delta = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - lastSeenAt);
    const qint64 minutes = delta / 60000;
    if (minutes < 1) {
        return DevicesDialog::tr("just now");
    }
    if (minutes < 60) {
        return DevicesDialog::tr("%1 min ago").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return DevicesDialog::tr("%1 h ago").arg(hours);
    }
    return DevicesDialog::tr("%1 d ago").arg(hours / 24);
}

} // namespace

DevicesDialog::DevicesDialog(services::PrivacyService *service, QWidget *parent)
    : QDialog(parent), m_service(service)
{
    setObjectName(QStringLiteral("devicesDialog"));
    setWindowTitle(tr("Sessions and devices"));
    setModal(true);
    resize(620, 460);

    auto *hint = new QLabel(
        tr("Ending a session revokes its access token, marks the device as revoked "
           "and deletes its one-time keys. The next request from it asks to sign in again."),
        this);
    hint->setProperty("miniRole", QStringLiteral("caption"));
    hint->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("devicesList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_revoke = new QPushButton(tr("End session"), this);
    m_revoke->setObjectName(QStringLiteral("revokeDeviceButton"));
    m_revoke->setCursor(Qt::PointingHandCursor);
    m_revoke->setEnabled(false);

    m_revokeAll = new QPushButton(tr("End all other sessions"), this);
    m_revokeAll->setObjectName(QStringLiteral("revokeAllDevicesButton"));
    m_revokeAll->setCursor(Qt::PointingHandCursor);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("devicesStatus"));
    m_status->setProperty("miniRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addWidget(m_revoke);
    bottom->addWidget(m_revokeAll);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(hint);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_status);
    layout->addLayout(bottom);

    connect(m_revoke, &QPushButton::clicked, this, &DevicesDialog::revokeSelected);
    connect(m_revokeAll, &QPushButton::clicked, this, &DevicesDialog::revokeAll);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const int row = m_list->currentRow();
        const bool other = row >= 0 && row < m_devices.size() && !m_devices.at(row).current;
        m_revoke->setEnabled(other);
    });
    reload();
}

void DevicesDialog::reload()
{
    if (!m_service) {
        m_status->setText(tr("Devices are not available."));
        return;
    }
    m_status->setText(tr("Loading…"));
    QPointer<DevicesDialog> self(this);
    m_service->listDevices([this, self](Result<QVector<services::DeviceEntry>> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_status->setText(tr("Could not load devices: %1").arg(result.error()));
            return;
        }
        m_devices = result.value();
        m_list->clear();
        bool hasOther = false;
        for (const services::DeviceEntry &device : std::as_const(m_devices)) {
            const QString name = device.name.isEmpty() ? device.deviceId : device.name;
            const QString platform = device.platform.isEmpty() ? tr("unknown platform")
                                                               : device.platform;
            QString text = QStringLiteral("%1 — %2 — %3")
                               .arg(name, platform, lastSeenText(device.lastSeenAt));
            if (!device.ip.isEmpty()) {
                text += QStringLiteral(" — %1").arg(device.ip);
            }
            if (device.current) {
                text += QStringLiteral(" — ") + tr("this device");
            } else {
                hasOther = true;
            }
            auto *item = new QListWidgetItem(text, m_list);
            item->setData(Qt::UserRole, device.deviceId);
        }
        m_revokeAll->setEnabled(hasOther);
        m_status->setText(m_devices.isEmpty() ? tr("No active devices.")
                                              : tr("Active devices: %1").arg(m_devices.size()));
    });
}

void DevicesDialog::revokeSelected()
{
    if (!m_service) {
        return;
    }
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_devices.size() || m_devices.at(row).current) {
        return;
    }
    const QString deviceId = m_devices.at(row).deviceId;
    m_revoke->setEnabled(false);
    QPointer<DevicesDialog> self(this);
    m_service->revokeDevice(deviceId, [this, self](Result<bool> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_status->setText(tr("Could not end the session: %1").arg(result.error()));
            return;
        }
        reload();
    });
}

void DevicesDialog::revokeAll()
{
    if (!m_service) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("End all other sessions"),
        tr("End every session except this device? Other devices will have to sign in again."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_revokeAll->setEnabled(false);
    QPointer<DevicesDialog> self(this);
    m_service->revokeAllOtherSessions([this, self](Result<int> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_revokeAll->setEnabled(true);
            m_status->setText(tr("Could not end the sessions: %1").arg(result.error()));
            return;
        }
        m_status->setText(tr("Sessions ended: %1").arg(result.value()));
        reload();
    });
}

} // namespace mini
