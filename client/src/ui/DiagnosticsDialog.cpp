#include "ui/DiagnosticsDialog.h"

#include "app/Paths.h"
#include "services/DiagnosticsService.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace mini {

namespace {

QString labelFor(const QString &id)
{
    if (id == QLatin1String("version")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Client version");
    }
    if (id == QLatin1String("address")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Address");
    }
    if (id == QLatin1String("dns")) {
        return QStringLiteral("DNS");
    }
    if (id == QLatin1String("tcp")) {
        return QStringLiteral("TCP");
    }
    if (id == QLatin1String("tls")) {
        return QStringLiteral("TLS");
    }
    if (id == QLatin1String("ws")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "WebSocket");
    }
    if (id == QLatin1String("ping")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Ping");
    }
    if (id == QLatin1String("server")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Server");
    }
    if (id == QLatin1String("database")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Database");
    }
    if (id == QLatin1String("cache")) {
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Cache");
    }
    return id;
}

QString statusWord(services::DiagnosticStatus status)
{
    switch (status) {
    case services::DiagnosticStatus::Ok:
        return QCoreApplication::translate("mini::DiagnosticsDialog", "OK");
    case services::DiagnosticStatus::Fail:
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Problem");
    case services::DiagnosticStatus::Unknown:
        return QCoreApplication::translate("mini::DiagnosticsDialog", "Not checked");
    }
    return QString();
}

} // namespace

DiagnosticsDialog::DiagnosticsDialog(services::DiagnosticsService *service, QWidget *parent)
    : QDialog(parent), m_service(service), m_logDirectory(Paths::logsDir())
{
    setObjectName(QStringLiteral("diagnosticsDialog"));
    setWindowTitle(tr("Diagnostics"));
    setModal(true);
    resize(560, 480);

    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("diagnosticsTable"));
    m_table->setHorizontalHeaderLabels({tr("Indicator"), tr("Value")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAccessibleName(tr("Diagnostics"));

    m_reconnect = new QPushButton(tr("Reconnect"), this);
    m_reconnect->setObjectName(QStringLiteral("reconnectButton"));
    m_copy = new QPushButton(tr("Copy report"), this);
    m_copy->setObjectName(QStringLiteral("copyReportButton"));
    m_openLogs = new QPushButton(tr("Open log folder"), this);
    m_openLogs->setObjectName(QStringLiteral("openLogsButton"));
    for (QPushButton *button : {m_reconnect, m_copy, m_openLogs}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
    }

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(m_reconnect);
    buttons->addWidget(m_copy);
    buttons->addWidget(m_openLogs);
    buttons->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);

    connect(m_reconnect, &QPushButton::clicked, this, [this] {
        if (m_service) {
            m_service->reconnect();
        }
    });
    connect(m_copy, &QPushButton::clicked, this, &DiagnosticsDialog::copyReport);
    connect(m_openLogs, &QPushButton::clicked, this, &DiagnosticsDialog::openLogFolder);

    if (m_service) {
        connect(m_service, &services::DiagnosticsService::refreshed, this,
                &DiagnosticsDialog::populate);
    }
    populate();
    refresh();
}

void DiagnosticsDialog::setLogDirectory(const QString &directory)
{
    m_logDirectory = directory;
}

void DiagnosticsDialog::setUrlOpener(std::function<bool(const QString &)> opener)
{
    m_urlOpener = std::move(opener);
}

void DiagnosticsDialog::refresh()
{
    if (m_service) {
        m_service->refresh();
    }
}

void DiagnosticsDialog::populate()
{
    if (!m_service) {
        return;
    }
    const services::DiagnosticsSnapshot snapshot = m_service->snapshot();
    m_table->setRowCount(snapshot.items.size());
    for (int row = 0; row < snapshot.items.size(); ++row) {
        const services::DiagnosticItem &item = snapshot.items.at(row);
        auto *name = new QTableWidgetItem(labelFor(item.id));
        QString value = statusWord(item.status);
        if (!item.detail.isEmpty()) {
            value = QStringLiteral("%1 — %2").arg(value, item.detail);
        }
        auto *detail = new QTableWidgetItem(value);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, detail);
    }
}

void DiagnosticsDialog::copyReport()
{
    if (!m_service) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(m_service->report());
    }
    emit reportCopied();
}

void DiagnosticsDialog::openLogFolder()
{
    if (m_urlOpener) {
        (void)m_urlOpener(m_logDirectory);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDirectory));
    }
    emit logFolderOpened(m_logDirectory);
}

} // namespace mini
