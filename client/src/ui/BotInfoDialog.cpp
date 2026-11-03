#include "ui/BotInfoDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

BotInfoDialog::BotInfoDialog(services::BotService *service, const services::BotInfo &bot,
                             QWidget *parent)
    : QDialog(parent), m_service(service), m_bot(bot)
{
    setObjectName(QStringLiteral("botInfoDialog"));
    setWindowTitle(tr("Bot information"));
    setModal(true);
    resize(500, 380);

    m_name = new QLabel(bot.displayName.isEmpty() ? bot.login : bot.displayName, this);
    m_name->setObjectName(QStringLiteral("botInfoName"));
    m_name->setProperty("miniRole", QStringLiteral("title"));
    m_name->setWordWrap(true);

    m_login = new QLabel(QStringLiteral("@") + bot.login, this);
    m_login->setObjectName(QStringLiteral("botInfoLogin"));
    m_login->setProperty("miniRole", QStringLiteral("subtitle"));

    m_about = new QLabel(bot.about.isEmpty() ? tr("No description") : bot.about, this);
    m_about->setObjectName(QStringLiteral("botInfoAbout"));
    m_about->setWordWrap(true);

    m_prefix = new QLabel(
        bot.tokenPrefix.isEmpty() ? tr("Token: not issued")
                                  : tr("Token prefix: %1…").arg(bot.tokenPrefix),
        this);
    m_prefix->setObjectName(QStringLiteral("botInfoTokenPrefix"));
    m_prefix->setProperty("miniRole", QStringLiteral("caption"));

    auto *flags = new QLabel(
        tr("Groups: %1 · Inline: %2 · Not end-to-end encrypted")
            .arg(bot.canJoinGroups ? tr("allowed") : tr("off"),
                 bot.inlineEnabled ? tr("on") : tr("off")),
        this);
    flags->setProperty("miniRole", QStringLiteral("caption"));
    flags->setWordWrap(true);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("botInfoStatus"));
    m_status->setProperty("miniRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);

    m_rotate = new QPushButton(tr("Rotate token"), this);
    m_rotate->setObjectName(QStringLiteral("botRotateButton"));
    m_rotate->setCursor(Qt::PointingHandCursor);
    m_rotate->setEnabled(service != nullptr && !bot.botId.isEmpty());

    m_delete = new QPushButton(tr("Delete bot"), this);
    m_delete->setObjectName(QStringLiteral("botDeleteButton"));
    m_delete->setCursor(Qt::PointingHandCursor);
    m_delete->setEnabled(service != nullptr && !bot.botId.isEmpty());

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addWidget(m_rotate);
    actions->addWidget(m_delete);
    actions->addStretch(1);

    auto *facts = new QFormLayout();
    facts->setSpacing(8);
    facts->addRow(tr("Login"), m_login);
    facts->addRow(tr("Description"), m_about);
    facts->addRow(tr("Token"), m_prefix);
    facts->addRow(tr("Access"), flags);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(m_name);
    layout->addLayout(facts);
    layout->addWidget(m_status);
    layout->addLayout(actions);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(m_rotate, &QPushButton::clicked, this, &BotInfoDialog::rotate);
    connect(m_delete, &QPushButton::clicked, this, &BotInfoDialog::remove);
}

void BotInfoDialog::rotate()
{
    if (!m_service || m_bot.botId.isEmpty()) {
        return;
    }
    m_rotate->setEnabled(false);
    m_status->setText(tr("Issuing a new token…"));
    QPointer<BotInfoDialog> self(this);
    m_service->rotateToken(
        m_bot.botId, [this, self](Result<services::BotToken> result) {
            if (!self) {
                return;
            }
            m_rotate->setEnabled(true);
            if (result.isError()) {
                m_status->setText(tr("Could not rotate the token: %1").arg(result.error()));
                return;
            }
            m_status->clear();
            m_prefix->setText(tr("Token prefix: %1…").arg(result.value().prefix));
            emit tokenIssued(result.value());
        });
}

void BotInfoDialog::remove()
{
    if (!m_service || m_bot.botId.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete bot"),
        tr("Delete %1? The token stops working and the chat history with the bot "
           "is removed.")
            .arg(m_bot.displayName.isEmpty() ? m_bot.login : m_bot.displayName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_delete->setEnabled(false);
    QPointer<BotInfoDialog> self(this);
    m_service->deleteBot(m_bot.botId, [this, self](Result<bool> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_delete->setEnabled(true);
            m_status->setText(tr("Could not delete the bot: %1").arg(result.error()));
            return;
        }
        emit botDeleted(m_bot.botId);
        accept();
    });
}

} // namespace mini
