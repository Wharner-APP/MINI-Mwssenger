#include "ui/AboutDialog.h"

#include "core/BuildInfo.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About MINI Messenger"));
    setModal(true);
    resize(460, 420);

    m_version = new QLabel(tr("MINI Messenger %1").arg(BuildInfo::appVersion()), this);
    m_version->setObjectName(QStringLiteral("aboutVersion"));
    m_version->setProperty("miniRole", QStringLiteral("title"));

    auto *versions = new QLabel(
        tr("Qt %1\nlibsodium %2\nSQLite %3")
            .arg(BuildInfo::qtVersion(), BuildInfo::sodiumVersion(), BuildInfo::sqliteVersion()),
        this);
    versions->setProperty("miniRole", QStringLiteral("subtitle"));

    auto *fonts = new QLabel(tr("Fonts: Manrope, Inter, JetBrains Mono (SIL OFL 1.1)"), this);
    fonts->setProperty("miniRole", QStringLiteral("caption"));
    fonts->setWordWrap(true);

    m_licenses = new QLabel(
        tr("Licenses: Qt (LGPL-3.0), libsodium (ISC), SQLite (public domain), "
           "fonts (SIL OFL 1.1)"),
        this);
    m_licenses->setObjectName(QStringLiteral("aboutLicenses"));
    m_licenses->setProperty("miniRole", QStringLiteral("caption"));
    m_licenses->setWordWrap(true);

    auto *limits = new QLabel(
        tr("Not promised: anonymity, PC protection, hiding that you use MINI Messenger, "
           "history without sign.in, work without the internet, a mobile app, real payments."),
        this);
    limits->setProperty("miniRole", QStringLiteral("caption"));
    limits->setWordWrap(true);

    m_disclaimer = new QLabel(
        tr("MINI Messenger is an educational project and is not affiliated with MINI/BMW."),
        this);
    m_disclaimer->setObjectName(QStringLiteral("aboutDisclaimer"));
    m_disclaimer->setProperty("miniRole", QStringLiteral("caption"));
    m_disclaimer->setWordWrap(true);

    auto *close = new QPushButton(tr("Close"), this);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::StrongFocus);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *bottom = new QHBoxLayout();
    bottom->addStretch(1);
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(m_version);
    layout->addWidget(versions);
    layout->addSpacing(8);
    layout->addWidget(fonts);
    layout->addWidget(m_licenses);
    layout->addWidget(limits);
    layout->addWidget(m_disclaimer);
    layout->addStretch(1);
    layout->addLayout(bottom);
}

} // namespace mini
