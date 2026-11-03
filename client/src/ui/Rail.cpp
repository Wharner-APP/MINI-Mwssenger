#include "ui/Rail.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>

namespace mini {

namespace {

QToolButton *makeRailButton(const QString &objectName, const QString &label, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setFixedSize(48, 48);
    button->setIconSize(QSize(24, 24));
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(label);
    button->setToolTip(label);
    return button;
}

} // namespace

Rail::Rail(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("rail"));
    setFixedWidth(72);

    m_chats = makeRailButton(QStringLiteral("railChats"), tr("Chats"), this);
    m_chats->setCheckable(true);
    m_chats->setChecked(true);

    m_contacts = makeRailButton(QStringLiteral("railContacts"), tr("Contacts"), this);
    m_contacts->setCheckable(true);

    m_settings = makeRailButton(QStringLiteral("railSettings"), tr("Settings"), this);
    m_profile = makeRailButton(QStringLiteral("railProfile"), tr("Profile"), this);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_chats);
    group->addButton(m_contacts);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_chats);
    layout->addWidget(m_contacts);
    layout->addStretch(1);
    layout->addWidget(m_settings);
    layout->addWidget(m_profile);

    connect(m_chats, &QToolButton::clicked, this, &Rail::chatsRequested);
    connect(m_contacts, &QToolButton::clicked, this, &Rail::contactsRequested);
    connect(m_settings, &QToolButton::clicked, this, &Rail::settingsRequested);
    connect(m_profile, &QToolButton::clicked, this, &Rail::profileRequested);
    connect(m_chats, &QToolButton::toggled, this, &Rail::refreshIcons);
    connect(m_contacts, &QToolButton::toggled, this, &Rail::refreshIcons);
    connect(&Theme::instance(), &Theme::changed, this, &Rail::refreshIcons);

    refreshIcons();
}

void Rail::refreshIcons()
{
    const Tokens t = Theme::instance().tokens();
    const QColor active = t.accent;
    const QColor idle = t.textSecondary;

    m_chats->setIcon(Icons::icon(QStringLiteral("chats"),
                                 m_chats->isChecked() ? active : idle, 24));
    m_contacts->setIcon(Icons::icon(QStringLiteral("contacts"),
                                    m_contacts->isChecked() ? active : idle, 24));
    m_settings->setIcon(Icons::icon(QStringLiteral("settings"), idle, 24));
    m_profile->setIcon(Icons::icon(QStringLiteral("user"), idle, 24));
}

} // namespace mini
