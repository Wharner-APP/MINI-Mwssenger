#include "ui/SettingsDialog.h"

#include "core/BuildInfo.h"
#include "core/Settings.h"
#include "services/DiagnosticsService.h"
#include "storage/StorageWorker.h"
#include "ui/AboutDialog.h"
#include "ui/Theme.h"
#include "ui/widgets/ColorSwatch.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
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

    layout->addSpacing(12);

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
    connect(m_backup, &QPushButton::clicked, this, &SettingsDialog::runBackup);
    connect(m_diagnostics, &QPushButton::clicked, this, &SettingsDialog::diagnosticsRequested);
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
