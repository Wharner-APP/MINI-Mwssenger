#pragma once

#include <QDialog>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimeEdit;

namespace mini {

class ColorSwatch;
class Settings;

namespace services {
class DiagnosticsService;
class ExportService;
class ImportService;
class NotificationService;
class PrivacyService;
}

namespace storage {
class StorageWorker;
}

// Theme, accent, font scale, density, close behaviour and the About page.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);

    void setBackupTarget(storage::StorageWorker *worker, const QString &keysPath,
                         const QString &backupsDir);
    void setDiagnosticsService(services::DiagnosticsService *service);
    // Wires the Privacy tab (§11.12 #177-182). Null keeps the controls but
    // disables editing.
    void setPrivacyService(services::PrivacyService *service);
    // §46.4: global notification switches and the Do Not Disturb schedule.
    void setNotificationService(services::NotificationService *service);
    // §49: encrypted export and merge import.
    void setExportService(services::ExportService *service);
    void setImportService(services::ImportService *service);

    [[nodiscard]] QComboBox *themeCombo() const { return m_theme; }
    [[nodiscard]] QComboBox *scaleCombo() const { return m_scale; }
    [[nodiscard]] QComboBox *densityCombo() const { return m_density; }
    [[nodiscard]] ColorSwatch *accentSwatch(int index) const;
    [[nodiscard]] QCheckBox *closeToTrayCheckBox() const { return m_closeToTray; }
    [[nodiscard]] QPushButton *backupButton() const { return m_backup; }
    [[nodiscard]] QPushButton *diagnosticsButton() const { return m_diagnostics; }
    [[nodiscard]] QPushButton *aboutButton() const { return m_about; }
    [[nodiscard]] QPushButton *exportButton() const { return m_exportButton; }
    [[nodiscard]] QPushButton *importButton() const { return m_importButton; }
    [[nodiscard]] QProgressBar *transferProgress() const { return m_transferProgress; }
    [[nodiscard]] QComboBox *languageCombo() const { return m_language; }

    // §46.4 notification controls.
    [[nodiscard]] QCheckBox *notificationsEnabledCheckBox() const
    {
        return m_notificationsEnabled;
    }
    [[nodiscard]] QCheckBox *notificationSoundCheckBox() const { return m_notificationSound; }
    [[nodiscard]] QCheckBox *notificationPreviewCheckBox() const
    {
        return m_notificationPreview;
    }
    [[nodiscard]] QCheckBox *dndScheduleCheckBox() const { return m_dndSchedule; }
    [[nodiscard]] QTimeEdit *dndStartEdit() const { return m_dndStart; }
    [[nodiscard]] QTimeEdit *dndEndEdit() const { return m_dndEnd; }

    // Privacy tab (§11.12 #177-182).
    [[nodiscard]] QComboBox *privacyMessageCombo() const { return m_privacyMessage; }
    [[nodiscard]] QComboBox *privacyCallCombo() const { return m_privacyCall; }
    [[nodiscard]] QComboBox *privacyLastSeenCombo() const { return m_privacyLastSeen; }
    [[nodiscard]] QCheckBox *privacyReceiptsCheckBox() const { return m_privacyReceipts; }
    [[nodiscard]] QPushButton *blockedUsersButton() const { return m_blockedUsers; }
    [[nodiscard]] QPushButton *devicesButton() const { return m_devices; }
    [[nodiscard]] QLabel *privacyStatusLabel() const { return m_privacyStatus; }

signals:
    void settingsChanged();
    void diagnosticsRequested();
    // §49.2: the chat list has to reload after rows were merged.
    void dataImported();

private:
    void buildAppearanceTab(QWidget *page);
    void buildBehaviourTab(QWidget *page);
    void buildNotificationsTab(QWidget *page);
    void buildPrivacyTab(QWidget *page);
    void buildAboutTab(QWidget *page);
    void applyFromControls();
    void loadPrivacy();
    void applyPrivacy();
    void openBlockedUsers();
    void openDevices();
    void openAbout();
    void runBackup();
    void startExport();
    void startImport();
    void setTransferBusy(bool busy);
    void loadNotificationSettings();

    Settings *m_settings = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_scale = nullptr;
    QComboBox *m_density = nullptr;
    QButtonGroup *m_accentGroup = nullptr;
    QVector<ColorSwatch *> m_swatches;

    QCheckBox *m_closeToTray = nullptr;
    QComboBox *m_language = nullptr;
    QPushButton *m_backup = nullptr;
    QPushButton *m_diagnostics = nullptr;
    QPushButton *m_about = nullptr;
    QLabel *m_backupStatus = nullptr;

    // §49 export/import.
    services::ExportService *m_exportService = nullptr;
    services::ImportService *m_importService = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QCheckBox *m_exportMedia = nullptr;
    QProgressBar *m_transferProgress = nullptr;
    QLabel *m_transferStatus = nullptr;
    QPushButton *m_transferCancel = nullptr;
    bool m_transferBusy = false;

    // §46.4 notifications and Do Not Disturb.
    services::NotificationService *m_notificationService = nullptr;
    QCheckBox *m_notificationsEnabled = nullptr;
    QCheckBox *m_notificationSound = nullptr;
    QCheckBox *m_notificationPreview = nullptr;
    QCheckBox *m_dndSchedule = nullptr;
    QTimeEdit *m_dndStart = nullptr;
    QTimeEdit *m_dndEnd = nullptr;
    QLabel *m_notificationStatus = nullptr;
    bool m_notificationLoading = false;

    services::PrivacyService *m_privacyService = nullptr;
    QComboBox *m_privacyMessage = nullptr;
    QComboBox *m_privacyCall = nullptr;
    QComboBox *m_privacyLastSeen = nullptr;
    QCheckBox *m_privacyReceipts = nullptr;
    QPushButton *m_blockedUsers = nullptr;
    QPushButton *m_devices = nullptr;
    QLabel *m_privacyStatus = nullptr;
    bool m_privacyLoading = false;

    storage::StorageWorker *m_storage = nullptr;
    QString m_keysPath;
    QString m_backupsDir;
};

} // namespace mini
