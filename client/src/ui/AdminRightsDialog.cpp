#include "ui/AdminRightsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {
namespace {

struct PermissionEntry
{
    quint32 bit;
    const char *title;
};

const PermissionEntry kEntries[] = {
    {group_permissions::EditInfo, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Edit group info")},
    {group_permissions::EditDescription,
     QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Edit description")},
    {group_permissions::DeleteMessages,
     QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Delete messages")},
    {group_permissions::Ban, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Ban members")},
    {group_permissions::Invite, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Invite members")},
    {group_permissions::Pin, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Pin messages")},
    {group_permissions::Voice, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Voice chats")},
    {group_permissions::AssignAdmins,
     QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Assign admins")},
    {group_permissions::SlowMode, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Slow mode")},
    {group_permissions::MentionAll, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Mention all")},
    {group_permissions::Stories, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Stories")},
    {group_permissions::Approve, QT_TRANSLATE_NOOP("mini::AdminRightsDialog", "Approve requests")},
};

} // namespace

AdminRightsDialog::AdminRightsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("adminRightsDialog"));
    setWindowTitle(tr("Administrator rights"));
    setModal(true);

    auto *intro = new QLabel(tr("Choose what this administrator may do."), this);
    intro->setWordWrap(true);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    int index = 0;
    for (const PermissionEntry &entry : kEntries) {
        auto *box = new QCheckBox(tr(entry.title), this);
        box->setObjectName(QStringLiteral("adminPermission%1").arg(entry.bit));
        box->setProperty("permissionBit", static_cast<uint>(entry.bit));
        m_boxes.append(box);
        grid->addWidget(box, index / 2, index % 2);
        ++index;
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    layout->addWidget(intro);
    layout->addLayout(grid);
    layout->addWidget(buttons);
}

void AdminRightsDialog::setPermissions(quint32 permissions)
{
    for (QCheckBox *box : m_boxes) {
        const quint32 bit = box->property("permissionBit").toUInt();
        box->setChecked((permissions & bit) != 0);
    }
}

quint32 AdminRightsDialog::permissions() const
{
    quint32 mask = 0;
    for (QCheckBox *box : m_boxes) {
        if (box->isChecked()) {
            mask |= box->property("permissionBit").toUInt();
        }
    }
    return mask & group_permissions::All;
}

} // namespace mini
