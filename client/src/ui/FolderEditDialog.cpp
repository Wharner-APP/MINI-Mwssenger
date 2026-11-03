#include "ui/FolderEditDialog.h"

#include "services/FolderService.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

FolderEditDialog::FolderEditDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("folderEditDialog"));
    setWindowTitle(tr("Folder"));
    setModal(true);

    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("folderTitle"));
    m_title->setMaxLength(64);

    auto *includeBox = new QGroupBox(tr("Include chats"), this);
    auto *includeLayout = new QVBoxLayout(includeBox);
    m_direct = new QCheckBox(tr("Personal"), includeBox);
    m_group = new QCheckBox(tr("Groups"), includeBox);
    m_channel = new QCheckBox(tr("Channels"), includeBox);
    m_bot = new QCheckBox(tr("Bots"), includeBox);
    includeLayout->addWidget(m_direct);
    includeLayout->addWidget(m_group);
    includeLayout->addWidget(m_channel);
    includeLayout->addWidget(m_bot);

    auto *excludeBox = new QGroupBox(tr("Exclude chats"), this);
    auto *excludeLayout = new QVBoxLayout(excludeBox);
    m_excludeDirect = new QCheckBox(tr("Personal"), excludeBox);
    m_excludeGroup = new QCheckBox(tr("Groups"), excludeBox);
    m_excludeChannel = new QCheckBox(tr("Channels"), excludeBox);
    excludeLayout->addWidget(m_excludeDirect);
    excludeLayout->addWidget(m_excludeGroup);
    excludeLayout->addWidget(m_excludeChannel);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("folderName"));
    m_name->setMaxLength(64);
    m_name->setPlaceholderText(tr("Name contains…"));

    m_onlyUnread = new QCheckBox(tr("Only unread"), this);
    m_onlyUnread->setObjectName(QStringLiteral("folderOnlyUnread"));
    m_onlyMuted = new QCheckBox(tr("Only muted"), this);
    m_onlyMuted->setObjectName(QStringLiteral("folderOnlyMuted"));

    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #d9534f;"));

    auto *form = new QFormLayout();
    form->addRow(tr("Name"), m_title);
    form->addRow(tr("Name filter"), m_name);
    form->addRow(QString(), m_onlyUnread);
    form->addRow(QString(), m_onlyMuted);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FolderEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &FolderEditDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(includeBox);
    layout->addWidget(excludeBox);
    layout->addWidget(m_error);
    layout->addWidget(buttons);
    resize(380, 460);
}

ChatFolder FolderEditDialog::folder() const
{
    ChatFolder folder;
    folder.title = m_title->text();
    folder.nameContains = m_name->text();
    folder.onlyUnread = m_onlyUnread->isChecked();
    folder.onlyMuted = m_onlyMuted->isChecked();
    if (m_direct->isChecked()) {
        folder.includeTypes.append(QStringLiteral("direct"));
    }
    if (m_group->isChecked()) {
        folder.includeTypes.append(QStringLiteral("group"));
    }
    if (m_channel->isChecked()) {
        folder.includeTypes.append(QStringLiteral("channel"));
    }
    if (m_bot->isChecked()) {
        folder.includeTypes.append(QStringLiteral("bot"));
    }
    if (m_excludeDirect->isChecked()) {
        folder.excludeTypes.append(QStringLiteral("direct"));
    }
    if (m_excludeGroup->isChecked()) {
        folder.excludeTypes.append(QStringLiteral("group"));
    }
    if (m_excludeChannel->isChecked()) {
        folder.excludeTypes.append(QStringLiteral("channel"));
    }
    return folder;
}

void FolderEditDialog::setFolder(const ChatFolder &folder)
{
    m_title->setText(folder.title);
    m_name->setText(folder.nameContains);
    m_onlyUnread->setChecked(folder.onlyUnread);
    m_onlyMuted->setChecked(folder.onlyMuted);
    m_direct->setChecked(folder.includeTypes.contains(QStringLiteral("direct")));
    m_group->setChecked(folder.includeTypes.contains(QStringLiteral("group")));
    m_channel->setChecked(folder.includeTypes.contains(QStringLiteral("channel")));
    m_bot->setChecked(folder.includeTypes.contains(QStringLiteral("bot")));
    m_excludeDirect->setChecked(folder.excludeTypes.contains(QStringLiteral("direct")));
    m_excludeGroup->setChecked(folder.excludeTypes.contains(QStringLiteral("group")));
    m_excludeChannel->setChecked(folder.excludeTypes.contains(QStringLiteral("channel")));
}

void FolderEditDialog::accept()
{
    auto valid = services::FolderService::validate(folder());
    if (valid.isError()) {
        m_error->setText(valid.error());
        return;
    }
    m_error->clear();
    QDialog::accept();
}

} // namespace mini
