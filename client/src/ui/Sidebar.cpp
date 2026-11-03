#include "ui/Sidebar.h"

#include "ui/ChatList.h"
#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace mini {

Sidebar::Sidebar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setMinimumWidth(260);
    setMaximumWidth(420);

    auto *title = new QLabel(tr("Chats"), this);
    title->setProperty("miniRole", QStringLiteral("title"));

    m_newChat = new QToolButton(this);
    m_newChat->setObjectName(QStringLiteral("newChatButton"));
    m_newChat->setFixedSize(36, 36);
    m_newChat->setIconSize(QSize(20, 20));
    m_newChat->setFocusPolicy(Qt::StrongFocus);
    m_newChat->setCursor(Qt::PointingHandCursor);
    m_newChat->setAccessibleName(tr("New chat"));
    m_newChat->setToolTip(tr("New chat"));

    auto *newMenu = new QMenu(m_newChat);
    newMenu->setObjectName(QStringLiteral("newChatMenu"));
    QAction *newChatAction = newMenu->addAction(tr("New chat"));
    QAction *newGroupAction = newMenu->addAction(tr("New group"));
    QAction *joinGroupAction = newMenu->addAction(tr("Join by invite link"));
    QAction *newChannelAction = newMenu->addAction(tr("New channel"));
    QAction *joinChannelAction = newMenu->addAction(tr("Join channel by invite link"));
    m_newChat->setMenu(newMenu);
    m_newChat->setPopupMode(QToolButton::InstantPopup);
    connect(newChatAction, &QAction::triggered, this, &Sidebar::newChatRequested);
    connect(newGroupAction, &QAction::triggered, this, &Sidebar::newGroupRequested);
    connect(newChannelAction, &QAction::triggered, this, &Sidebar::newChannelRequested);
    connect(joinGroupAction, &QAction::triggered, this, &Sidebar::joinGroupRequested);
    connect(joinChannelAction, &QAction::triggered, this, &Sidebar::joinChannelRequested);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    header->addWidget(title, 1);
    header->addWidget(m_newChat);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("chatSearch"));
    m_search->setPlaceholderText(tr("Search chats"));
    m_search->setAccessibleName(tr("Search chats"));
    m_search->setClearButtonEnabled(true);
    m_searchAction = new QAction(m_search);
    m_search->addAction(m_searchAction, QLineEdit::LeadingPosition);

    m_chatList = new ChatList(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 0);
    layout->setSpacing(10);
    layout->addLayout(header);
    layout->addWidget(m_search);
    layout->addWidget(m_chatList, 1);

    connect(m_search, &QLineEdit::textChanged, m_chatList, &ChatList::setFilter);
    connect(m_chatList, &ChatList::newChatRequested, this, &Sidebar::newChatRequested);
    connect(m_chatList, &ChatList::chatSelected, this, &Sidebar::chatSelected);
    connect(&Theme::instance(), &Theme::changed, this, &Sidebar::refreshIcons);

    refreshIcons();
}

void Sidebar::refreshIcons()
{
    const Tokens t = Theme::instance().tokens();
    m_newChat->setIcon(Icons::icon(QStringLiteral("plus"), t.textPrimary, 20));
    m_searchAction->setIcon(Icons::icon(QStringLiteral("search"), t.textTertiary, 18));
}

} // namespace mini
