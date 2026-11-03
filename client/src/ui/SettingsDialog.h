#pragma once

#include <QDialog>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace mini {

class ColorSwatch;
class Settings;

namespace services {
class DiagnosticsService;
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

    [[nodiscard]] QComboBox *themeCombo() const { return m_theme; }
    [[nodiscard]] QComboBox *scaleCombo() const { return m_scale; }
    [[nodiscard]] QComboBox *densityCombo() const { return m_density; }
    [[nodiscard]] ColorSwatch *accentSwatch(int index) const;
    [[nodiscard]] QCheckBox *closeToTrayCheckBox() const { return m_closeToTray; }
    [[nodiscard]] QPushButton *backupButton() const { return m_backup; }
    [[nodiscard]] QPushButton *diagnosticsButton() const { return m_diagnostics; }
    [[nodiscard]] QPushButton *aboutButton() const { return m_about; }

signals:
    void settingsChanged();
    void diagnosticsRequested();

private:
    void buildAppearanceTab(QWidget *page);
    void buildBehaviourTab(QWidget *page);
    void buildAboutTab(QWidget *page);
    void applyFromControls();
    void openAbout();
    void runBackup();

    Settings *m_settings = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_scale = nullptr;
    QComboBox *m_density = nullptr;
    QButtonGroup *m_accentGroup = nullptr;
    QVector<ColorSwatch *> m_swatches;

    QCheckBox *m_closeToTray = nullptr;
    QPushButton *m_backup = nullptr;
    QPushButton *m_diagnostics = nullptr;
    QPushButton *m_about = nullptr;
    QLabel *m_backupStatus = nullptr;

    storage::StorageWorker *m_storage = nullptr;
    QString m_keysPath;
    QString m_backupsDir;
};

} // namespace mini
