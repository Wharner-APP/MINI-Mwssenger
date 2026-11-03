#include "ui/BotCreateDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace mini {

BotCreateDialog::BotCreateDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("botCreateDialog"));
    setWindowTitle(tr("Create a bot"));
    setModal(true);
    resize(500, 430);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("botNameEdit"));
    m_name->setMaxLength(32);
    m_name->setPlaceholderText(tr("Bot name"));
    m_name->setAccessibleName(tr("Bot name"));

    m_login = new QLineEdit(this);
    m_login->setObjectName(QStringLiteral("botLoginEdit"));
    m_login->setMaxLength(20);
    m_login->setPlaceholderText(tr("helper"));
    m_login->setAccessibleName(tr("Bot login"));
    auto *loginValidator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[a-z0-9_]{0,20}")), m_login);
    m_login->setValidator(loginValidator);

    auto *suffix = new QLabel(QStringLiteral("_bot"), this);
    suffix->setProperty("miniRole", QStringLiteral("caption"));

    auto *loginRow = new QHBoxLayout();
    loginRow->setContentsMargins(0, 0, 0, 0);
    loginRow->setSpacing(4);
    loginRow->addWidget(m_login, 1);
    loginRow->addWidget(suffix);

    m_about = new QPlainTextEdit(this);
    m_about->setObjectName(QStringLiteral("botAboutEdit"));
    m_about->setPlaceholderText(tr("Description (up to 255 characters)"));
    m_about->setMaximumHeight(90);

    m_groups = new QCheckBox(tr("Allow the bot in groups"), this);
    m_groups->setObjectName(QStringLiteral("botGroupsCheck"));
    m_groups->setChecked(true);

    m_inline = new QCheckBox(tr("Inline mode (if available)"), this);
    m_inline->setObjectName(QStringLiteral("botInlineCheck"));

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("botCreateError"));
    m_error->setProperty("miniRole", QStringLiteral("caption"));
    m_error->setWordWrap(true);
    m_error->setVisible(false);

    auto *form = new QFormLayout();
    form->setSpacing(10);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Login"), loginRow);
    form->addRow(tr("Description"), m_about);
    form->addRow(QString(), m_groups);
    form->addRow(QString(), m_inline);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    m_create = buttons->button(QDialogButtonBox::Ok);
    m_create->setObjectName(QStringLiteral("botCreateButton"));
    m_create->setText(tr("Create"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &BotCreateDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_login, &QLineEdit::textChanged, this, [this](const QString &) {
        m_error->setVisible(false);
    });
}

services::BotDraft BotCreateDialog::draft() const
{
    services::BotDraft draft;
    draft.displayName = m_name->text().trimmed();
    draft.login = m_login->text().trimmed().toLower();
    draft.about = m_about->toPlainText().trimmed();
    draft.canJoinGroups = m_groups->isChecked();
    draft.inlineEnabled = m_inline->isChecked();
    return draft;
}

void BotCreateDialog::setDraft(const services::BotDraft &draft)
{
    m_name->setText(draft.displayName);
    QString login = draft.login;
    if (login.endsWith(QLatin1String("_bot"))) {
        login.chop(4);
    }
    m_login->setText(login);
    m_about->setPlainText(draft.about);
    m_groups->setChecked(draft.canJoinGroups);
    m_inline->setChecked(draft.inlineEnabled);
}

void BotCreateDialog::setError(const QString &text)
{
    m_error->setText(text);
    m_error->setVisible(!text.isEmpty());
}

QString BotCreateDialog::normalizedLogin() const
{
    return services::BotService::normalizeLogin(m_login->text());
}

void BotCreateDialog::submit()
{
    const Result<services::BotDraft> valid =
        services::BotService::validateDraft(draft());
    if (valid.isError()) {
        if (valid.error() == QLatin1String("bot.name_invalid")) {
            setError(tr("The name must be 3 to 32 characters."));
        } else if (valid.error() == QLatin1String("bot.login_invalid")) {
            setError(tr("The login must be 1 to 20 lowercase latin letters, digits or "
                        "underscores and start with a letter."));
        } else if (valid.error() == QLatin1String("bot.about_too_long")) {
            setError(tr("The description must be at most 255 characters."));
        } else {
            setError(valid.error());
        }
        return;
    }
    setError(QString());
    accept();
}

} // namespace mini
