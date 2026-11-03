#include "ui/BotsDialog.h"

#include "services/BotService.h"
#include "ui/BotCreateDialog.h"
#include "ui/BotInfoDialog.h"
#include "ui/BotTokenDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace mini {

namespace {

// One row: avatar initial, name, @login and the two actions S24 asks for.
QWidget *makeRow(const services::BotInfo &bot,
                 const std::function<void()> &open,
                 const std::function<void()> &details, const std::function<void()> &remove)
{
    auto *row = new QWidget();
    row->setObjectName(QStringLiteral("botRow"));

    auto *initial = new QLabel(bot.displayName.isEmpty()
                                   ? bot.login.left(1).toUpper()
                                   : bot.displayName.left(1).toUpper(),
                               row);
    initial->setFixedSize(36, 36);
    initial->setAlignment(Qt::AlignCenter);
    initial->setProperty("miniRole", QStringLiteral("avatar"));

    auto *name = new QLabel(bot.displayName.isEmpty() ? bot.login : bot.displayName, row);
    name->setProperty("miniRole", QStringLiteral("subtitle"));
    auto *login = new QLabel(QStringLiteral("@") + bot.login, row);
    login->setProperty("miniRole", QStringLiteral("caption"));

    auto *text = new QVBoxLayout();
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(1);
    text->addWidget(name);
    text->addWidget(login);

    auto *openButton = new QPushButton(BotsDialog::tr("Open"), row);
    openButton->setObjectName(QStringLiteral("botOpenButton"));
    openButton->setCursor(Qt::PointingHandCursor);

    auto *menuButton = new QToolButton(row);
    menuButton->setObjectName(QStringLiteral("botRowMenuButton"));
    menuButton->setText(QStringLiteral("⋯"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(menuButton);
    QAction *detailsAction = menu->addAction(BotsDialog::tr("Information"));
    QAction *deleteAction = menu->addAction(BotsDialog::tr("Delete"));
    menuButton->setMenu(menu);

    QObject::connect(openButton, &QPushButton::clicked, row, open);
    QObject::connect(detailsAction, &QAction::triggered, row, details);
    QObject::connect(deleteAction, &QAction::triggered, row, remove);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);
    layout->addWidget(initial);
    layout->addLayout(text, 1);
    layout->addWidget(openButton);
    layout->addWidget(menuButton);
    return row;
}

} // namespace

BotsDialog::BotsDialog(services::BotService *service, QWidget *parent)
    : QDialog(parent), m_service(service)
{
    setObjectName(QStringLiteral("botsDialog"));
    setWindowTitle(tr("My bots"));
    setModal(true);
    resize(560, 520);

    auto *title = new QLabel(tr("My bots"), this);
    title->setProperty("miniRole", QStringLiteral("title"));

    m_warning = new QLabel(
        tr("Messages to a bot are not end-to-end encrypted. The bot owner can read them."),
        this);
    m_warning->setObjectName(QStringLiteral("botsWarning"));
    m_warning->setProperty("miniRole", QStringLiteral("caption"));
    m_warning->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("botsList"));
    m_list->setAlternatingRowColors(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("botsStatus"));
    m_status->setProperty("miniRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);

    m_create = new QPushButton(tr("Create a bot"), this);
    m_create->setObjectName(QStringLiteral("botsCreateButton"));
    m_create->setCursor(Qt::PointingHandCursor);
    m_create->setEnabled(service != nullptr);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addWidget(m_create);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_warning);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_status);
    layout->addLayout(bottom);

    connect(m_create, &QPushButton::clicked, this, &BotsDialog::createBot);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item) {
                    openBot(item->data(Qt::UserRole).toString());
                }
            });
    reload();
}

void BotsDialog::reload()
{
    m_list->clear();
    m_bots.clear();
    if (!m_service) {
        m_status->setText(tr("Bots are not available."));
        return;
    }
    m_status->setText(tr("Loading…"));
    QPointer<BotsDialog> self(this);
    m_service->listBots([this, self](Result<QVector<services::BotInfo>> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_status->setText(tr("Could not load bots: %1").arg(result.error()));
            return;
        }
        m_bots = result.value();
        m_list->clear();
        for (const services::BotInfo &bot : std::as_const(m_bots)) {
            auto *item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, bot.botId);
            item->setSizeHint(QSize(480, 56));
            m_list->setItemWidget(item, makeRow(
                                            bot,
                                            [this, id = bot.botId] { openBot(id); },
                                            [this, id = bot.botId] { showDetails(id); },
                                            [this, id = bot.botId] { showDetails(id); }));
        }
        m_status->setText(m_bots.isEmpty()
                              ? tr("You have no bots yet.")
                              : tr("Bots: %1 of 20").arg(m_bots.size()));
    });
}

services::BotInfo BotsDialog::botById(const QString &botId) const
{
    for (const services::BotInfo &bot : m_bots) {
        if (bot.botId == botId) {
            return bot;
        }
    }
    return {};
}

void BotsDialog::openBot(const QString &botId)
{
    const services::BotInfo bot = botById(botId);
    if (bot.botId.isEmpty()) {
        return;
    }
    emit chatRequested(bot);
    accept();
}

void BotsDialog::createBot()
{
    if (!m_service) {
        return;
    }
    BotCreateDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_create->setEnabled(false);
    m_status->setText(tr("Creating…"));
    QPointer<BotsDialog> self(this);
    m_service->createBot(
        dialog.draft(), [this, self](Result<services::BotCreateResult> result) {
            if (!self) {
                return;
            }
            m_create->setEnabled(true);
            if (result.isError()) {
                m_status->setText(tr("Could not create the bot: %1").arg(result.error()));
                return;
            }
            // The token is shown exactly once, right after creation.
            BotTokenDialog token(result.value().token, this);
            token.exec();
            reload();
        });
}

void BotsDialog::showDetails(const QString &botId)
{
    const services::BotInfo bot = botById(botId);
    if (bot.botId.isEmpty()) {
        return;
    }
    BotInfoDialog dialog(m_service, bot, this);
    connect(&dialog, &BotInfoDialog::tokenIssued, this,
            [this](const services::BotToken &token) {
                BotTokenDialog tokenDialog(token, this);
                tokenDialog.exec();
            });
    connect(&dialog, &BotInfoDialog::botDeleted, this, [this](const QString &) {
        reload();
    });
    dialog.exec();
    reload();
}

} // namespace mini
