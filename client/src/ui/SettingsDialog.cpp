#include "ui/SettingsDialog.h"

#include "core/BuildInfo.h"
#include "core/Settings.h"
#include "services/DiagnosticsService.h"
#include "services/ExportService.h"
#include "services/ImportService.h"
#include "services/NotificationService.h"
#include "services/PrivacyService.h"
#include "storage/StorageWorker.h"
#include "ui/AboutDialog.h"
#include "ui/BlockListDialog.h"
#include "ui/DevicesDialog.h"
#include "ui/Theme.h"
#include "ui/widgets/ColorSwatch.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace mini {

namespace {

constexpr auto kThemeKey = "ui/theme";
constexpr auto kAccentKey = "ui/accent";
constexpr auto kFontScaleKey = "ui/font_scale";
constexpr auto kDensityKey = "ui/density";
constexpr auto kCloseToTrayKey = "ui/close_to_tray";

} // namespace

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(560, 560);

    auto *tabs = new QTabWidget(this);
    auto *appearance = new QWidget(tabs);
    buildAppearanceTab(appearance);
    tabs->addTab(appearance, tr("Appearance"));

    auto *behaviour = new QWidget(tabs);
    buildBehaviourTab(behaviour);
    tabs->addTab(behaviour, tr("Behaviour"));

    auto *notifications = new QWidget(tabs);
    buildNotificationsTab(notifications);
    tabs->addTab(notifications, tr("Notifications"));

    auto *privacy = new QWidget(tabs);
    buildPrivacyTab(privacy);
    tabs->addTab(privacy, tr("Privacy"));

    auto *about = new QWidget(tabs);
    buildAboutTab(about);
    tabs->addTab(about, tr("About"));

    auto *close = new QPushButton(tr("Close"), this);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::StrongFocus);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *bottom = new QHBoxLayout();
    bottom->addStretch(1);
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(tabs, 1);
    layout->addLayout(bottom);
}

void SettingsDialog::buildAppearanceTab(QWidget *page)
{
    auto *form = new QFormLayout();
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(12);

    m_theme = new QComboBox(this);
    m_theme->addItem(tr("Dark"), Theme::modeId(ThemeMode::Dark));
    m_theme->addItem(tr("Light"), Theme::modeId(ThemeMode::Light));
    m_theme->addItem(tr("AMOLED"), Theme::modeId(ThemeMode::Amoled));
    m_theme->setCurrentIndex(m_theme->findData(Theme::modeId(Theme::instance().mode())));
    m_theme->setAccessibleName(tr("Theme"));
    m_theme->setFocusPolicy(Qt::StrongFocus);
    form->addRow(tr("Theme"), m_theme);

    auto *accentWidget = new QWidget(this);
    auto *accentGrid = new QGridLayout(accentWidget);
    accentGrid->setContentsMargins(0, 0, 0, 0);
    accentGrid->setSpacing(8);
    m_accentGroup = new QButtonGroup(this);
    m_accentGroup->setExclusive(true);

    const QVector<AccentDef> accents = Theme::accents();
    for (int i = 0; i < accents.size(); ++i) {
        const AccentDef &def = accents.at(i);
        auto *swatch = new ColorSwatch(def.color, accentWidget);
        swatch->setToolTip(def.name);
        swatch->setAccessibleName(def.name);
        swatch->setChecked(Theme::instance().accent() == Theme::accentFromId(def.id));
        m_accentGroup->addButton(swatch, i);
        accentGrid->addWidget(swatch, i / 4, i % 4);
        m_swatches.append(swatch);
    }
    accentGrid->setColumnStretch(4, 1);
    form->addRow(tr("Accent"), accentWidget);

    m_scale = new QComboBox(this);
    for (const int scale : {90, 100, 112}) {
        m_scale->addItem(QStringLiteral("%1%").arg(scale), scale);
    }
    m_scale->setCurrentIndex(m_scale->findData(Theme::instance().fontScalePercent()));
    m_scale->setAccessibleName(tr("Font size"));
    form->addRow(tr("Font size"), m_scale);

    m_density = new QComboBox(this);
    m_density->addItem(tr("Comfortable"), Theme::densityId(Density::Comfortable));
    m_density->addItem(tr("Compact"), Theme::densityId(Density::Compact));
    m_density->setCurrentIndex(m_density->findData(Theme::densityId(Theme::instance().density())));
    m_density->setAccessibleName(tr("Density"));
    form->addRow(tr("Density"), m_density);

    auto *hint = new QLabel(tr("Changes apply immediately."), this);
    hint->setProperty("miniRole", QStringLiteral("caption"));
    form->addRow(QString(), hint);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->addLayout(form);
    pageLayout->addStretch(1);

    connect(m_theme, &QComboBox::currentIndexChanged, this, [this] { applyFromControls(); });
    connect(m_scale, &QComboBox::currentIndexChanged, this, [this] { applyFromControls(); });
    connect(m_density, &QComboBox::currentIndexChanged, this, [this] { applyFromControls(); });
    connect(m_accentGroup, &QButtonGroup::idClicked, this, [this] { applyFromControls(); });
}

void SettingsDialog::buildBehaviourTab(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_closeToTray = new QCheckBox(tr("Close to tray"), this);
    m_closeToTray->setObjectName(QStringLiteral("closeToTrayCheckBox"));
    m_closeToTray->setChecked(m_settings
                                  ? m_settings->getBool(kCloseToTrayKey, true)
                                  : true);
    m_closeToTray->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_closeToTray);

    auto *hint = new QLabel(tr("The tray menu offers Open, Do not disturb and Exit."), this);
    hint->setProperty("miniRole", QStringLiteral("caption"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    layout->addSpacing(8);

    // §12.5: one of the two shipped catalogs; applied on the next launch.
    auto *languageRow = new QHBoxLayout();
    languageRow->setContentsMargins(0, 0, 0, 0);
    languageRow->setSpacing(8);
    auto *languageLabel = new QLabel(tr("Language"), this);
    m_language = new QComboBox(this);
    m_language->setObjectName(QStringLiteral("languageCombo"));
    m_language->addItem(tr("Russian"), QStringLiteral("ru"));
    m_language->addItem(tr("English"), QStringLiteral("en"));
    const QString language =
        m_settings ? m_settings->get(QStringLiteral("ui/language"), QStringLiteral("ru"))
                   : QStringLiteral("ru");
    m_language->setCurrentIndex(
        qMax(0, m_language->findData(language.trimmed().startsWith(QLatin1String("en"),
                                                                   Qt::CaseInsensitive)
                                         ? QStringLiteral("en")
                                         : QStringLiteral("ru"))));
    languageRow->addWidget(languageLabel);
    languageRow->addWidget(m_language, 1);
    layout->addLayout(languageRow);

    auto *languageHint =
        new QLabel(tr("Interface language changes after a restart."), this);
    languageHint->setProperty("miniRole", QStringLiteral("caption"));
    languageHint->setWordWrap(true);
    layout->addWidget(languageHint);

    layout->addSpacing(8);

    auto *dataTitle = new QLabel(tr("Data"), this);
    dataTitle->setProperty("miniRole", QStringLiteral("subtitle"));
    layout->addWidget(dataTitle);

    m_backup = new QPushButton(tr("Back up now"), this);
    m_backup->setObjectName(QStringLiteral("backupNowButton"));
    m_backup->setCursor(Qt::PointingHandCursor);
    m_backup->setFocusPolicy(Qt::StrongFocus);
    m_backup->setEnabled(false);
    m_backupStatus = new QLabel(this);
    m_backupStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_backupStatus->setWordWrap(true);

    auto *dataRow = new QHBoxLayout();
    dataRow->setContentsMargins(0, 0, 0, 0);
    dataRow->addWidget(m_backup);
    dataRow->addWidget(m_backupStatus, 1);
    layout->addLayout(dataRow);

    // §49.1 / §49.2: encrypted archive export and merge import.
    m_exportButton = new QPushButton(tr("Export"), this);
    m_exportButton->setObjectName(QStringLiteral("exportBackupButton"));
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setFocusPolicy(Qt::StrongFocus);
    m_exportButton->setEnabled(false);
    m_importButton = new QPushButton(tr("Import"), this);
    m_importButton->setObjectName(QStringLiteral("importBackupButton"));
    m_importButton->setCursor(Qt::PointingHandCursor);
    m_importButton->setFocusPolicy(Qt::StrongFocus);
    m_importButton->setEnabled(false);
    m_exportMedia = new QCheckBox(tr("Include media"), this);
    m_exportMedia->setObjectName(QStringLiteral("exportMediaCheckBox"));
    m_exportMedia->setChecked(false);

    auto *transferRow = new QHBoxLayout();
    transferRow->setContentsMargins(0, 0, 0, 0);
    transferRow->setSpacing(8);
    transferRow->addWidget(m_exportButton);
    transferRow->addWidget(m_importButton);
    transferRow->addWidget(m_exportMedia);
    transferRow->addStretch(1);
    layout->addLayout(transferRow);

    m_transferProgress = new QProgressBar(this);
    m_transferProgress->setObjectName(QStringLiteral("transferProgress"));
    m_transferProgress->setRange(0, 100);
    m_transferProgress->setValue(0);
    m_transferProgress->setVisible(false);
    m_transferCancel = new QPushButton(tr("Cancel"), this);
    m_transferCancel->setObjectName(QStringLiteral("transferCancelButton"));
    m_transferCancel->setCursor(Qt::PointingHandCursor);
    m_transferCancel->setVisible(false);

    auto *progressRow = new QHBoxLayout();
    progressRow->setContentsMargins(0, 0, 0, 0);
    progressRow->setSpacing(8);
    progressRow->addWidget(m_transferProgress, 1);
    progressRow->addWidget(m_transferCancel, 0);
    layout->addLayout(progressRow);

    m_transferStatus = new QLabel(this);
    m_transferStatus->setObjectName(QStringLiteral("transferStatusLabel"));
    m_transferStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_transferStatus->setWordWrap(true);
    layout->addWidget(m_transferStatus);

    m_diagnostics = new QPushButton(tr("Diagnostics"), this);
    m_diagnostics->setObjectName(QStringLiteral("diagnosticsButton"));
    m_diagnostics->setCursor(Qt::PointingHandCursor);
    m_diagnostics->setFocusPolicy(Qt::StrongFocus);
    m_diagnostics->setEnabled(false);
    layout->addWidget(m_diagnostics);

    layout->addStretch(1);

    connect(m_closeToTray, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings) {
            m_settings->setBool(kCloseToTrayKey, checked);
            m_settings->flush();
        }
        emit settingsChanged();
    });
    connect(m_language, &QComboBox::currentIndexChanged, this, [this] {
        if (m_settings) {
            const QString language = m_language->currentData().toString();
            m_settings->set(QStringLiteral("ui/language"), language);
            m_settings->set(QStringLiteral("ui.language"), language);
            m_settings->flush();
        }
    });
    connect(m_backup, &QPushButton::clicked, this, &SettingsDialog::runBackup);
    connect(m_exportButton, &QPushButton::clicked, this, &SettingsDialog::startExport);
    connect(m_importButton, &QPushButton::clicked, this, &SettingsDialog::startImport);
    connect(m_transferCancel, &QPushButton::clicked, this, [this] {
        if (m_exportService && m_transferBusy) {
            m_exportService->cancel();
        }
        if (m_importService && m_transferBusy) {
            m_importService->cancel();
        }
    });
    connect(m_diagnostics, &QPushButton::clicked, this, &SettingsDialog::diagnosticsRequested);
}

void SettingsDialog::buildNotificationsTab(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_notificationsEnabled = new QCheckBox(tr("Enable notifications"), this);
    m_notificationsEnabled->setObjectName(QStringLiteral("notificationsEnabledCheckBox"));
    m_notificationSound = new QCheckBox(tr("Play a sound"), this);
    m_notificationSound->setObjectName(QStringLiteral("notificationSoundCheckBox"));
    m_notificationPreview = new QCheckBox(tr("Show message preview"), this);
    m_notificationPreview->setObjectName(QStringLiteral("notificationPreviewCheckBox"));
    layout->addWidget(m_notificationsEnabled);
    layout->addWidget(m_notificationSound);
    layout->addWidget(m_notificationPreview);

    layout->addSpacing(8);

    auto *dndTitle = new QLabel(tr("Do not disturb"), this);
    dndTitle->setProperty("miniRole", QStringLiteral("subtitle"));
    layout->addWidget(dndTitle);

    m_dndSchedule = new QCheckBox(tr("Quiet hours"), this);
    m_dndSchedule->setObjectName(QStringLiteral("dndScheduleCheckBox"));
    layout->addWidget(m_dndSchedule);

    auto *timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(0, 0, 0, 0);
    timeRow->setSpacing(8);
    m_dndStart = new QTimeEdit(this);
    m_dndStart->setObjectName(QStringLiteral("dndStartEdit"));
    m_dndStart->setDisplayFormat(QStringLiteral("HH:mm"));
    m_dndEnd = new QTimeEdit(this);
    m_dndEnd->setObjectName(QStringLiteral("dndEndEdit"));
    m_dndEnd->setDisplayFormat(QStringLiteral("HH:mm"));
    timeRow->addWidget(new QLabel(tr("From"), this));
    timeRow->addWidget(m_dndStart);
    timeRow->addWidget(new QLabel(tr("To"), this));
    timeRow->addWidget(m_dndEnd);
    timeRow->addStretch(1);
    layout->addLayout(timeRow);

    m_notificationStatus = new QLabel(tr("Notification settings are unavailable."), this);
    m_notificationStatus->setObjectName(QStringLiteral("notificationStatusLabel"));
    m_notificationStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_notificationStatus->setWordWrap(true);
    layout->addWidget(m_notificationStatus);
    layout->addStretch(1);

    const auto apply = [this] {
        if (!m_notificationService || m_notificationLoading) {
            return;
        }
        m_notificationService->setNotificationsEnabled(m_notificationsEnabled->isChecked());
        m_notificationService->setSoundEnabled(m_notificationSound->isChecked());
        m_notificationService->setPreviewEnabled(m_notificationPreview->isChecked());
        m_notificationService->setDndScheduleEnabled(m_dndSchedule->isChecked());
        m_notificationService->setDndStart(m_dndStart->time());
        m_notificationService->setDndEnd(m_dndEnd->time());
        m_notificationStatus->setText(tr("Saved."));
        emit settingsChanged();
    };
    connect(m_notificationsEnabled, &QCheckBox::toggled, this, apply);
    connect(m_notificationSound, &QCheckBox::toggled, this, apply);
    connect(m_notificationPreview, &QCheckBox::toggled, this, apply);
    connect(m_dndSchedule, &QCheckBox::toggled, this, apply);
    connect(m_dndStart, &QTimeEdit::timeChanged, this, apply);
    connect(m_dndEnd, &QTimeEdit::timeChanged, this, apply);

    for (QWidget *control : {static_cast<QWidget *>(m_notificationsEnabled),
                             static_cast<QWidget *>(m_notificationSound),
                             static_cast<QWidget *>(m_notificationPreview),
                             static_cast<QWidget *>(m_dndSchedule),
                             static_cast<QWidget *>(m_dndStart),
                             static_cast<QWidget *>(m_dndEnd)}) {
        control->setEnabled(false);
    }
}

void SettingsDialog::buildPrivacyTab(QWidget *page)
{
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);

    const auto fillRules = [](QComboBox *combo) {
        combo->addItem(tr("Everyone"), services::PrivacyService::ruleId(
                                           services::PrivacyRule::Everyone));
        combo->addItem(tr("Contacts"), services::PrivacyService::ruleId(
                                           services::PrivacyRule::Contacts));
        combo->addItem(tr("Nobody"), services::PrivacyService::ruleId(
                                         services::PrivacyRule::Nobody));
    };

    m_privacyMessage = new QComboBox(this);
    m_privacyMessage->setObjectName(QStringLiteral("privacyMessageCombo"));
    fillRules(m_privacyMessage);
    form->addRow(tr("Who can write to me"), m_privacyMessage);

    m_privacyCall = new QComboBox(this);
    m_privacyCall->setObjectName(QStringLiteral("privacyCallCombo"));
    fillRules(m_privacyCall);
    form->addRow(tr("Who can call me"), m_privacyCall);

    m_privacyLastSeen = new QComboBox(this);
    m_privacyLastSeen->setObjectName(QStringLiteral("privacyLastSeenCombo"));
    fillRules(m_privacyLastSeen);
    form->addRow(tr("Who sees my activity"), m_privacyLastSeen);

    m_privacyReceipts = new QCheckBox(tr("Send read receipts"), this);
    m_privacyReceipts->setObjectName(QStringLiteral("privacyReceiptsCheck"));
    m_privacyReceipts->setChecked(true);
    form->addRow(QString(), m_privacyReceipts);

    auto *hint = new QLabel(
        tr("\"Contacts\" means people who share a group with you. Blocked users "
           "cannot write or call you regardless of these rules."),
        this);
    hint->setProperty("miniRole", QStringLiteral("caption"));
    hint->setWordWrap(true);

    m_blockedUsers = new QPushButton(tr("Blocked users"), this);
    m_blockedUsers->setObjectName(QStringLiteral("blockedUsersButton"));
    m_blockedUsers->setCursor(Qt::PointingHandCursor);
    m_blockedUsers->setEnabled(false);

    m_devices = new QPushButton(tr("Sessions and devices"), this);
    m_devices->setObjectName(QStringLiteral("devicesButton"));
    m_devices->setCursor(Qt::PointingHandCursor);
    m_devices->setEnabled(false);

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addWidget(m_blockedUsers);
    actions->addWidget(m_devices);
    actions->addStretch(1);

    m_privacyStatus = new QLabel(tr("Privacy settings are unavailable."), this);
    m_privacyStatus->setObjectName(QStringLiteral("privacyStatusLabel"));
    m_privacyStatus->setProperty("miniRole", QStringLiteral("caption"));
    m_privacyStatus->setWordWrap(true);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(m_privacyStatus);
    layout->addSpacing(6);
    layout->addLayout(actions);
    layout->addStretch(1);

    for (QComboBox *combo : {m_privacyMessage, m_privacyCall, m_privacyLastSeen}) {
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { applyPrivacy(); });
    }
    connect(m_privacyReceipts, &QCheckBox::toggled, this, [this] { applyPrivacy(); });
    connect(m_blockedUsers, &QPushButton::clicked, this,
            &SettingsDialog::openBlockedUsers);
    connect(m_devices, &QPushButton::clicked, this, &SettingsDialog::openDevices);
}

void SettingsDialog::buildAboutTab(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto *name = new QLabel(tr("MINI Messenger %1").arg(BuildInfo::appVersion()), this);
    name->setProperty("miniRole", QStringLiteral("title"));
    layout->addWidget(name);

    auto *versions = new QLabel(
        tr("Qt %1\nlibsodium %2\nSQLite %3")
            .arg(BuildInfo::qtVersion(), BuildInfo::sodiumVersion(), BuildInfo::sqliteVersion()),
        this);
    versions->setProperty("miniRole", QStringLiteral("subtitle"));
    layout->addWidget(versions);

    auto *fonts = new QLabel(tr("Fonts: Manrope, Inter, JetBrains Mono (SIL OFL 1.1)"), this);
    fonts->setProperty("miniRole", QStringLiteral("caption"));
    fonts->setWordWrap(true);
    layout->addWidget(fonts);

    auto *licenses = new QLabel(
        tr("Licenses: Qt (LGPL-3.0), libsodium (ISC), SQLite (public domain), fonts (SIL OFL 1.1)"),
        this);
    licenses->setProperty("miniRole", QStringLiteral("caption"));
    licenses->setWordWrap(true);
    layout->addWidget(licenses);

    auto *limits = new QLabel(
        tr("Not promised: anonymity, PC protection, hiding that you use MINI Messenger, "
           "history without sign.in, work without the internet, a mobile app, real payments."),
        this);
    limits->setProperty("miniRole", QStringLiteral("caption"));
    limits->setWordWrap(true);
    layout->addWidget(limits);

    auto *disclaimer =
        new QLabel(tr("MINI Messenger is an educational project and is not affiliated with MINI/BMW."),
                   this);
    disclaimer->setProperty("miniRole", QStringLiteral("caption"));
    disclaimer->setWordWrap(true);
    layout->addWidget(disclaimer);

    m_about = new QPushButton(tr("About MINI Messenger"), this);
    m_about->setObjectName(QStringLiteral("aboutButton"));
    m_about->setCursor(Qt::PointingHandCursor);
    m_about->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_about, 0, Qt::AlignLeft);

    layout->addStretch(1);

    connect(m_about, &QPushButton::clicked, this, &SettingsDialog::openAbout);
}

ColorSwatch *SettingsDialog::accentSwatch(int index) const
{
    if (index < 0 || index >= m_swatches.size()) {
        return nullptr;
    }
    return m_swatches.at(index);
}

void SettingsDialog::setPrivacyService(services::PrivacyService *service)
{
    m_privacyService = service;
    const bool ready = service != nullptr;
    m_privacyMessage->setEnabled(ready);
    m_privacyCall->setEnabled(ready);
    m_privacyLastSeen->setEnabled(ready);
    m_privacyReceipts->setEnabled(ready);
    m_blockedUsers->setEnabled(ready);
    m_devices->setEnabled(ready);
    if (!ready) {
        m_privacyStatus->setText(tr("Privacy settings are unavailable."));
        return;
    }
    loadPrivacy();
}

void SettingsDialog::loadPrivacy()
{
    if (!m_privacyService) {
        return;
    }
    m_privacyStatus->setText(tr("Loading privacy settings…"));
    QPointer<SettingsDialog> self(this);
    m_privacyService->loadSettings([this, self](Result<services::PrivacySettings> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_privacyStatus->setText(
                tr("Could not load privacy settings: %1").arg(result.error()));
            return;
        }
        const services::PrivacySettings settings = result.value();
        m_privacyLoading = true;
        m_privacyMessage->setCurrentIndex(m_privacyMessage->findData(
            services::PrivacyService::ruleId(settings.whoCanMessage)));
        m_privacyCall->setCurrentIndex(m_privacyCall->findData(
            services::PrivacyService::ruleId(settings.whoCanCall)));
        m_privacyLastSeen->setCurrentIndex(m_privacyLastSeen->findData(
            services::PrivacyService::ruleId(settings.whoSeesLastSeen)));
        m_privacyReceipts->setChecked(settings.readReceipts);
        m_privacyLoading = false;
        m_privacyStatus->setText(tr("Saved on the server."));
    });
}

void SettingsDialog::applyPrivacy()
{
    if (!m_privacyService || m_privacyLoading) {
        return;
    }
    services::PrivacySettings settings;
    settings.whoCanMessage = services::PrivacyService::ruleFromId(
        m_privacyMessage->currentData().toString());
    settings.whoCanCall =
        services::PrivacyService::ruleFromId(m_privacyCall->currentData().toString());
    settings.whoSeesLastSeen = services::PrivacyService::ruleFromId(
        m_privacyLastSeen->currentData().toString());
    settings.readReceipts = m_privacyReceipts->isChecked();
    m_privacyStatus->setText(tr("Saving…"));
    QPointer<SettingsDialog> self(this);
    m_privacyService->saveSettings(
        settings, [this, self](Result<services::PrivacySettings> result) {
            if (!self) {
                return;
            }
            if (result.isError()) {
                m_privacyStatus->setText(
                    tr("Could not save privacy settings: %1").arg(result.error()));
                return;
            }
            m_privacyStatus->setText(tr("Saved on the server."));
        });
}

void SettingsDialog::openBlockedUsers()
{
    BlockListDialog dialog(m_privacyService, this);
    dialog.exec();
}

void SettingsDialog::openDevices()
{
    DevicesDialog dialog(m_privacyService, this);
    dialog.exec();
}

void SettingsDialog::setBackupTarget(storage::StorageWorker *worker, const QString &keysPath,
                                     const QString &backupsDir)
{
    m_storage = worker;
    m_keysPath = keysPath;
    m_backupsDir = backupsDir;
    m_backup->setEnabled(worker != nullptr);
}

void SettingsDialog::setDiagnosticsService(services::DiagnosticsService *service)
{
    m_diagnostics->setEnabled(service != nullptr);
}

void SettingsDialog::setNotificationService(services::NotificationService *service)
{
    m_notificationService = service;
    const bool ready = service != nullptr;
    for (QWidget *control : {static_cast<QWidget *>(m_notificationsEnabled),
                             static_cast<QWidget *>(m_notificationSound),
                             static_cast<QWidget *>(m_notificationPreview),
                             static_cast<QWidget *>(m_dndSchedule),
                             static_cast<QWidget *>(m_dndStart),
                             static_cast<QWidget *>(m_dndEnd)}) {
        control->setEnabled(ready);
    }
    if (!ready) {
        m_notificationStatus->setText(tr("Notification settings are unavailable."));
        return;
    }
    loadNotificationSettings();
}

void SettingsDialog::loadNotificationSettings()
{
    if (!m_notificationService) {
        return;
    }
    m_notificationLoading = true;
    const QSignalBlocker blockEnabled(m_notificationsEnabled);
    const QSignalBlocker blockSound(m_notificationSound);
    const QSignalBlocker blockPreview(m_notificationPreview);
    const QSignalBlocker blockSchedule(m_dndSchedule);
    const QSignalBlocker blockStart(m_dndStart);
    const QSignalBlocker blockEnd(m_dndEnd);
    m_notificationsEnabled->setChecked(m_notificationService->notificationsEnabled());
    m_notificationSound->setChecked(m_notificationService->soundEnabled());
    m_notificationPreview->setChecked(m_notificationService->previewEnabled());
    m_dndSchedule->setChecked(m_notificationService->dndScheduleEnabled());
    m_dndStart->setTime(m_notificationService->dndStart());
    m_dndEnd->setTime(m_notificationService->dndEnd());
    m_notificationLoading = false;
    m_notificationStatus->setText(tr("Saved."));
}

void SettingsDialog::setExportService(services::ExportService *service)
{
    if (m_exportService) {
        disconnect(m_exportService, nullptr, this, nullptr);
    }
    m_exportService = service;
    if (m_exportService) {
        connect(m_exportService, &services::ExportService::progressChanged, this,
                [this](int percent, const QString &) {
                    if (m_transferBusy) {
                        m_transferProgress->setValue(percent);
                    }
                });
    }
    setTransferBusy(m_transferBusy);
}

void SettingsDialog::setImportService(services::ImportService *service)
{
    if (m_importService) {
        disconnect(m_importService, nullptr, this, nullptr);
    }
    m_importService = service;
    if (m_importService) {
        connect(m_importService, &services::ImportService::progressChanged, this,
                [this](int percent, const QString &) {
                    if (m_transferBusy) {
                        m_transferProgress->setValue(percent);
                    }
                });
    }
    setTransferBusy(m_transferBusy);
}

void SettingsDialog::setTransferBusy(bool busy)
{
    m_transferBusy = busy;
    m_exportButton->setEnabled(m_exportService != nullptr && !busy);
    m_importButton->setEnabled(m_importService != nullptr && !busy);
    m_transferProgress->setVisible(busy);
    m_transferCancel->setVisible(busy);
    if (busy) {
        m_transferProgress->setValue(0);
    }
}

void SettingsDialog::startExport()
{
    if (!m_exportService || m_transferBusy) {
        return;
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save backup"),
        QDir(directory).filePath(QStringLiteral("mini-backup.minibackup")),
        tr("MINI backup (*.minibackup)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QLatin1String(".minibackup"), Qt::CaseInsensitive)) {
        path += QLatin1String(".minibackup");
    }

    services::ExportOptions options;
    options.outputPath = path;
    options.includeMedia = m_exportMedia->isChecked();
    options.includeSecretChats = false;

    setTransferBusy(true);
    m_transferStatus->setText(tr("Exporting…"));
    m_exportService->exportBackup(options, [this](Result<services::ExportSummary> result) {
        setTransferBusy(false);
        if (result.isError()) {
            m_transferStatus->setText(
                tr("Export failed: %1").arg(result.error()));
            return;
        }
        const services::ExportSummary summary = result.value();
        if (summary.cancelled) {
            m_transferStatus->setText(tr("Export cancelled."));
            return;
        }
        m_transferStatus->setText(
            tr("Export finished: %1 chats, %2 messages, %3 media.")
                .arg(summary.chatCount)
                .arg(summary.messageCount)
                .arg(summary.mediaCount));
    });
}

void SettingsDialog::startImport()
{
    if (!m_importService || m_transferBusy) {
        return;
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a backup"), directory, tr("MINI backup (*.minibackup)"));
    if (path.isEmpty()) {
        return;
    }

    services::ImportOptions options;
    options.inputPath = path;
    options.backupDir = m_backupsDir;
    options.restoreMedia = true;

    setTransferBusy(true);
    m_transferStatus->setText(tr("Importing…"));
    m_importService->importBackup(options, [this](Result<services::ImportSummary> result) {
        setTransferBusy(false);
        if (result.isError()) {
            m_transferStatus->setText(
                tr("Import failed: %1").arg(result.error()));
            return;
        }
        const services::ImportSummary summary = result.value();
        if (summary.cancelled) {
            m_transferStatus->setText(tr("Import cancelled."));
            return;
        }
        m_transferStatus->setText(
            tr("Import finished: %1 messages added, %2 skipped.")
                .arg(summary.messagesAdded)
                .arg(summary.messagesSkipped));
        if (summary.chatsAdded > 0 || summary.messagesAdded > 0) {
            emit dataImported();
        }
    });
}

void SettingsDialog::applyFromControls()
{
    const ThemeMode mode = Theme::modeFromId(m_theme->currentData().toString());
    const int accentIndex = qBound(0, m_accentGroup->checkedId(), 7);
    const Theme::Accent accent =
        Theme::accentFromId(Theme::accents().at(accentIndex).id);
    const int scale = m_scale->currentData().toInt();
    const Density density = Theme::densityFromId(m_density->currentData().toString());

    Theme::instance().apply(mode, accent, scale, density);

    if (m_settings) {
        m_settings->set(kThemeKey, Theme::modeId(mode));
        m_settings->set(kAccentKey, Theme::accentId(accent));
        m_settings->setInt(kFontScaleKey, scale);
        m_settings->set(kDensityKey, Theme::densityId(density));
        m_settings->flush();
    }
    emit settingsChanged();
}

void SettingsDialog::openAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void SettingsDialog::runBackup()
{
    if (!m_storage) {
        return;
    }
    m_backup->setEnabled(false);
    m_backupStatus->setText(tr("Backing up..."));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString folder = watcher->result();
        watcher->deleteLater();
        m_backup->setEnabled(true);
        m_backupStatus->setText(folder.isEmpty() ? tr("Backup failed.") : tr("Backup created."));
    });
    watcher->setFuture(m_storage->createBackup(m_keysPath, m_backupsDir, 3));
}

} // namespace mini
