#pragma once

#include <QDialog>
#include <QString>

#include <functional>

class QPushButton;
class QTableWidget;

namespace mini {

namespace services {
class DiagnosticsService;
}

// Diagnostics screen: ten indicators and the three actions from §4.7.
class DiagnosticsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DiagnosticsDialog(services::DiagnosticsService *service,
                               QWidget *parent = nullptr);

    [[nodiscard]] QTableWidget *table() const { return m_table; }
    [[nodiscard]] QPushButton *reconnectButton() const { return m_reconnect; }
    [[nodiscard]] QPushButton *copyReportButton() const { return m_copy; }
    [[nodiscard]] QPushButton *openLogsButton() const { return m_openLogs; }

    void setLogDirectory(const QString &directory);
    [[nodiscard]] QString logDirectory() const { return m_logDirectory; }
    void setUrlOpener(std::function<bool(const QString &)> opener);

    void refresh();

signals:
    void reportCopied();
    void logFolderOpened(const QString &path);

private:
    void populate();
    void copyReport();
    void openLogFolder();

    services::DiagnosticsService *m_service = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_reconnect = nullptr;
    QPushButton *m_copy = nullptr;
    QPushButton *m_openLogs = nullptr;
    QString m_logDirectory;
    std::function<bool(const QString &)> m_urlOpener;
};

} // namespace mini
