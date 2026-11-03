#include "ui/NewChannelDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

NewChannelDialog::NewChannelDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("newChannelDialog"));
    setWindowTitle(tr("New channel"));
    setModal(true);
    resize(460, 380);

    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("channelTitleEdit"));
    m_title->setMaxLength(128);
    m_title->setPlaceholderText(tr("Channel name"));

    m_about = new QPlainTextEdit(this);
    m_about->setObjectName(QStringLiteral("channelAboutEdit"));
    m_about->setPlaceholderText(tr("Description (optional)"));
    m_about->setMaximumHeight(110);

    m_public = new QCheckBox(tr("Public channel"), this);
    m_public->setObjectName(QStringLiteral("channelPublicCheck"));

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("channelErrorLabel"));
    m_error->setProperty("miniRole", QStringLiteral("caption"));
    m_error->setWordWrap(true);
    m_error->setVisible(false);

    auto *form = new QFormLayout();
    form->addRow(tr("Name"), m_title);
    form->addRow(tr("Description"), m_about);
    form->addRow(QString(), m_public);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &NewChannelDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void NewChannelDialog::setDraft(const services::ChannelDraft &draft)
{
    m_title->setText(draft.title);
    m_about->setPlainText(draft.about);
    m_public->setChecked(draft.isPublic);
}

services::ChannelDraft NewChannelDialog::draft() const
{
    services::ChannelDraft draft;
    draft.title = m_title->text().trimmed();
    draft.about = m_about->toPlainText().trimmed();
    draft.isPublic = m_public->isChecked();
    return draft;
}

void NewChannelDialog::setError(const QString &text)
{
    m_error->setText(text);
    m_error->setVisible(!text.isEmpty());
}

void NewChannelDialog::submit()
{
    const Result<services::ChannelDraft> valid =
        services::ChannelService::validateDraft(draft());
    if (valid.isError()) {
        setError(valid.error());
        return;
    }
    setError(QString());
    accept();
}

} // namespace mini
