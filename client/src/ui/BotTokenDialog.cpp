#include "ui/BotTokenDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

BotTokenDialog::BotTokenDialog(const services::BotToken &token, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("botTokenDialog"));
    setWindowTitle(tr("Bot token"));
    setModal(true);
    resize(520, 240);

    m_warning = new QLabel(
        tr("Copy this token now — it will not be shown again. "
           "Anyone with the token can control the bot."),
        this);
    m_warning->setObjectName(QStringLiteral("botTokenWarning"));
    m_warning->setProperty("miniRole", QStringLiteral("caption"));
    m_warning->setWordWrap(true);

    m_token = new QLineEdit(token.token, this);
    m_token->setObjectName(QStringLiteral("botTokenField"));
    m_token->setReadOnly(true);
    m_token->setCursorPosition(0);
    m_token->setAccessibleName(tr("Bot token"));

    m_copy = new QPushButton(tr("Copy"), this);
    m_copy->setObjectName(QStringLiteral("botTokenCopyButton"));
    m_copy->setCursor(Qt::PointingHandCursor);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(m_token, 1);
    row->addWidget(m_copy);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_token->text());
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(m_warning);
    layout->addLayout(row);
    layout->addStretch(1);
    layout->addWidget(buttons);
}

QString BotTokenDialog::token() const
{
    return m_token->text();
}

} // namespace mini
