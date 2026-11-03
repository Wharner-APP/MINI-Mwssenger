#include "ui/NewGroupDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace mini {

NewGroupDialog::NewGroupDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("newGroupDialog"));
    setWindowTitle(tr("New group"));
    setModal(true);
    resize(460, 460);

    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("groupTitleEdit"));
    m_title->setMaxLength(128);
    m_title->setPlaceholderText(tr("Group name"));

    m_about = new QPlainTextEdit(this);
    m_about->setObjectName(QStringLiteral("groupAboutEdit"));
    m_about->setPlaceholderText(tr("Description (optional)"));
    m_about->setMaximumHeight(90);

    m_members = new QPlainTextEdit(this);
    m_members->setObjectName(QStringLiteral("groupMembersEdit"));
    m_members->setPlaceholderText(tr("Logins, one per line"));
    m_members->setMaximumHeight(110);

    m_public = new QCheckBox(tr("Public group"), this);
    m_public->setObjectName(QStringLiteral("groupPublicCheck"));
    m_request = new QCheckBox(tr("Join by request"), this);
    m_request->setObjectName(QStringLiteral("groupRequestCheck"));

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("groupErrorLabel"));
    m_error->setProperty("miniRole", QStringLiteral("caption"));
    m_error->setWordWrap(true);
    m_error->setVisible(false);

    auto *form = new QFormLayout();
    form->addRow(tr("Name"), m_title);
    form->addRow(tr("Description"), m_about);
    form->addRow(tr("Members"), m_members);
    form->addRow(QString(), m_public);
    form->addRow(QString(), m_request);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &NewGroupDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void NewGroupDialog::setDraft(const services::GroupDraft &draft)
{
    m_title->setText(draft.title);
    m_about->setPlainText(draft.about);
    m_members->setPlainText(draft.members.join(QLatin1Char('\n')));
    m_public->setChecked(draft.isPublic);
    m_request->setChecked(draft.joinByRequest);
}

services::GroupDraft NewGroupDialog::draft() const
{
    services::GroupDraft draft;
    draft.title = m_title->text().trimmed();
    draft.about = m_about->toPlainText().trimmed();
    draft.isPublic = m_public->isChecked();
    draft.joinByRequest = m_request->isChecked();
    const QStringList lines = m_members->toPlainText().split(
        QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString login = line.trimmed();
        if (!login.isEmpty()) {
            draft.members.append(login);
        }
    }
    return draft;
}

void NewGroupDialog::setError(const QString &text)
{
    m_error->setText(text);
    m_error->setVisible(!text.isEmpty());
}

void NewGroupDialog::submit()
{
    const Result<services::GroupDraft> valid =
        services::GroupService::validateDraft(draft());
    if (valid.isError()) {
        setError(valid.error());
        return;
    }
    setError(QString());
    accept();
}

} // namespace mini
