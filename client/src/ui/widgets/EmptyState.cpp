#include "ui/widgets/EmptyState.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

EmptyState::EmptyState(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(8);
    layout->addStretch(1);

    m_icon = new QLabel(this);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setAccessibleName(QString());
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_title = new QLabel(this);
    m_title->setProperty("miniRole", QStringLiteral("title"));
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_description = new QLabel(this);
    m_description->setProperty("miniRole", QStringLiteral("subtitle"));
    m_description->setAlignment(Qt::AlignCenter);
    m_description->setWordWrap(true);
    layout->addWidget(m_description);

    m_action = new QPushButton(this);
    m_action->setProperty("miniRole", QStringLiteral("accent"));
    m_action->setCursor(Qt::PointingHandCursor);
    m_action->setFocusPolicy(Qt::StrongFocus);
    m_action->hide();
    connect(m_action, &QPushButton::clicked, this, &EmptyState::actionTriggered);
    layout->addWidget(m_action, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    connect(&Theme::instance(), &Theme::changed, this, [this] {
        refreshIcon();
        update();
    });
    refreshIcon();
}

void EmptyState::setIconName(const QString &name)
{
    m_iconName = name;
    refreshIcon();
}

void EmptyState::setTitle(const QString &title)
{
    m_title->setText(title);
}

void EmptyState::setDescription(const QString &description)
{
    m_description->setText(description);
}

void EmptyState::setActionText(const QString &text)
{
    m_action->setText(text);
    m_action->setVisible(!text.isEmpty());
}

QString EmptyState::title() const
{
    return m_title->text();
}

QString EmptyState::description() const
{
    return m_description->text();
}

void EmptyState::refreshIcon()
{
    if (m_iconName.isEmpty()) {
        m_icon->clear();
        return;
    }
    const Tokens t = Theme::instance().tokens();
    m_icon->setPixmap(Icons::pixmap(m_iconName, t.textTertiary, 48));
}

} // namespace mini
