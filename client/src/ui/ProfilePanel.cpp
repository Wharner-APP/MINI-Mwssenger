#include "ui/ProfilePanel.h"

#include "core/Clock.h"
#include "core/Settings.h"
#include "services/AuthService.h"
#include "ui/widgets/Avatar.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

namespace {

constexpr auto kLoginKey = "profile/login";
constexpr auto kNameKey = "profile/display_name";
constexpr auto kBioKey = "profile/bio";
constexpr auto kOnlineKey = "profile/online";
constexpr auto kLastActiveKey = "profile/last_active_at";

} // namespace

ProfilePanel::ProfilePanel(Settings *settings, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("profilePanel"));
    setWindowTitle(tr("Profile"));
    setModal(true);
    resize(420, 440);

    m_avatar = new Avatar(this);
    m_avatar->setFixedSize(64, 64);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("profileDisplayName"));
    m_name->setMaxLength(64);
    m_name->setAccessibleName(tr("Name"));
    m_name->setFocusPolicy(Qt::StrongFocus);

    m_login = new QLabel(this);
    m_login->setObjectName(QStringLiteral("profileLogin"));
    m_login->setProperty("miniRole", QStringLiteral("caption"));
    m_login->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_bio = new QPlainTextEdit(this);
    m_bio->setObjectName(QStringLiteral("profileBio"));
    m_bio->setPlaceholderText(tr("A few words about yourself"));
    m_bio->setAccessibleName(tr("Bio"));
    m_bio->setFixedHeight(96);

    m_counter = new QLabel(this);
    m_counter->setProperty("miniRole", QStringLiteral("caption"));

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("profileStatus"));
    m_status->setProperty("miniRole", QStringLiteral("subtitle"));

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("profileError"));
    m_error->setProperty("miniRole", QStringLiteral("caption"));
    m_error->setWordWrap(true);

    m_save = new QPushButton(tr("Save"), this);
    m_save->setObjectName(QStringLiteral("profileSave"));
    m_save->setCursor(Qt::PointingHandCursor);
    m_save->setFocusPolicy(Qt::StrongFocus);
    auto *close = new QPushButton(tr("Close"), this);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::StrongFocus);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(14);
    header->addWidget(m_avatar);
    auto *headerText = new QVBoxLayout();
    headerText->setContentsMargins(0, 0, 0, 0);
    headerText->setSpacing(4);
    headerText->addWidget(m_name);
    headerText->addWidget(m_login);
    headerText->addWidget(m_status);
    header->addLayout(headerText, 1);

    auto *bioRow = new QHBoxLayout();
    bioRow->setContentsMargins(0, 0, 0, 0);
    bioRow->addStretch(1);
    bioRow->addWidget(m_counter);

    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addStretch(1);
    bottom->addWidget(close);
    bottom->addWidget(m_save);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addLayout(header);
    layout->addWidget(new QLabel(tr("Bio"), this));
    layout->addWidget(m_bio);
    layout->addLayout(bioRow);
    layout->addWidget(m_error);
    layout->addStretch(1);
    layout->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_save, &QPushButton::clicked, this, &ProfilePanel::save);
    connect(m_bio, &QPlainTextEdit::textChanged, this, &ProfilePanel::refreshCounter);
    connect(m_name, &QLineEdit::textChanged, this, [this] {
        m_avatar->setInitials(
            services::ContactService::initialsFor(m_name->text(), m_profile.login));
    });

    restore();
}

void ProfilePanel::setContactService(services::ContactService *service)
{
    m_service = service;
}

void ProfilePanel::setProfile(const services::SelfProfile &profile)
{
    applyProfile(profile);
    persist();
}

void ProfilePanel::applyProfile(const services::SelfProfile &profile)
{
    m_profile = profile;
    m_name->setText(profile.displayName);
    m_login->setText(profile.login);
    m_bio->setPlainText(profile.bio);
    m_avatar->setInitials(services::ContactService::initialsFor(profile.displayName,
                                                                profile.login));
    refreshStatus();
    refreshCounter();
}

void ProfilePanel::restore()
{
    if (!m_settings) {
        refreshStatus();
        refreshCounter();
        return;
    }
    services::SelfProfile profile;
    profile.login = m_settings->get(kLoginKey);
    profile.displayName = m_settings->get(kNameKey);
    profile.bio = m_settings->get(kBioKey);
    profile.online = m_settings->getBool(kOnlineKey, false);
    profile.lastActiveAt = m_settings->get(kLastActiveKey).toLongLong();
    applyProfile(profile);
}

void ProfilePanel::persist()
{
    if (!m_settings) {
        return;
    }
    m_settings->set(kLoginKey, m_profile.login);
    m_settings->set(kNameKey, m_profile.displayName);
    m_settings->set(kBioKey, m_profile.bio);
    m_settings->setBool(kOnlineKey, m_profile.online);
    m_settings->set(kLastActiveKey, QString::number(m_profile.lastActiveAt));
    m_settings->flush();
}

void ProfilePanel::refreshStatus()
{
    if (m_profile.online) {
        m_status->setText(tr("Online"));
        return;
    }
    if (m_profile.lastActiveAt > 0) {
        const qint64 delta = Clock::nowMs() - m_profile.lastActiveAt;
        m_status->setText(
            tr("Last seen %1").arg(services::ContactService::relativeShort(delta)));
        return;
    }
    m_status->setText(tr("Offline"));
}

void ProfilePanel::refreshCounter()
{
    m_counter->setText(QStringLiteral("%1/200").arg(m_bio->toPlainText().toUcs4().size()));
}

void ProfilePanel::showError(const QString &code)
{
    if (code == QLatin1String("name.invalid")) {
        m_error->setText(tr("Enter a name from 1 to 64 characters."));
    } else if (code == QLatin1String("profile.bio_too_long")) {
        m_error->setText(tr("The bio is limited to 200 characters."));
    } else if (code == QLatin1String("profile.bio_invalid")) {
        m_error->setText(tr("The bio contains unsupported characters."));
    } else if (code == QLatin1String("profile_unavailable")) {
        m_error->setText(tr("The profile service is not connected yet."));
    } else {
        m_error->setText(tr("Could not save the changes."));
    }
}

void ProfilePanel::save()
{
    const QString name = m_name->text();
    const QString bio = m_bio->toPlainText();

    const Result<QString> validName = services::AuthService::validateDisplayName(name);
    if (validName.isError()) {
        showError(validName.error());
        return;
    }
    const Result<QString> validBio = services::ContactService::validateBio(bio);
    if (validBio.isError()) {
        showError(validBio.error());
        return;
    }

    m_error->clear();
    services::SelfProfile candidate = m_profile;
    candidate.displayName = validName.value();
    candidate.bio = validBio.value();

    if (m_service && m_service->hasSelfSource()) {
        m_service->updateSelf(candidate.displayName, candidate.bio,
                              [this](Result<services::SelfProfile> result) {
            if (result.isError()) {
                showError(result.error());
                return;
            }
            applyProfile(result.value());
            persist();
            emit profileUpdated(m_profile);
        });
        return;
    }

    applyProfile(candidate);
    persist();
    emit profileUpdated(m_profile);
}

} // namespace mini
