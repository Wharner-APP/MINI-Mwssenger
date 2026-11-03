#include "ui/BlockListDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

BlockListDialog::BlockListDialog(services::PrivacyService *service, QWidget *parent)
    : QDialog(parent), m_service(service)
{
    setObjectName(QStringLiteral("blockListDialog"));
    setWindowTitle(tr("Blocked users"));
    setModal(true);
    resize(480, 460);

    auto *hint = new QLabel(
        tr("Blocked users cannot write to you or call you. Their calls are rejected."),
        this);
    hint->setProperty("miniRole", QStringLiteral("caption"));
    hint->setWordWrap(true);

    m_login = new QLineEdit(this);
    m_login->setObjectName(QStringLiteral("blockLoginEdit"));
    m_login->setPlaceholderText(tr("Exact login"));
    m_login->setAccessibleName(tr("Login to block"));

    m_block = new QPushButton(tr("Block"), this);
    m_block->setObjectName(QStringLiteral("blockButton"));
    m_block->setCursor(Qt::PointingHandCursor);

    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->setSpacing(8);
    addRow->addWidget(m_login, 1);
    addRow->addWidget(m_block);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("blockList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_unblock = new QPushButton(tr("Unblock"), this);
    m_unblock->setObjectName(QStringLiteral("unblockButton"));
    m_unblock->setCursor(Qt::PointingHandCursor);
    m_unblock->setEnabled(false);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("blockStatus"));
    m_status->setProperty("miniRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addWidget(m_unblock);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(hint);
    layout->addLayout(addRow);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_status);
    layout->addLayout(bottom);

    connect(m_block, &QPushButton::clicked, this, &BlockListDialog::block);
    connect(m_unblock, &QPushButton::clicked, this, &BlockListDialog::unblockSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        m_unblock->setEnabled(m_list->currentItem() != nullptr);
    });
    connect(m_login, &QLineEdit::returnPressed, this, &BlockListDialog::block);
    reload();
}

void BlockListDialog::reload()
{
    if (!m_service) {
        m_status->setText(tr("Privacy settings are not available."));
        return;
    }
    m_status->setText(tr("Loading…"));
    QPointer<BlockListDialog> self(this);
    m_service->listBlocks([this, self](Result<QVector<services::BlockedUser>> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_status->setText(tr("Could not load the blacklist: %1").arg(result.error()));
            return;
        }
        m_blocked = result.value();
        m_list->clear();
        for (const services::BlockedUser &user : std::as_const(m_blocked)) {
            const QString name = user.displayName.isEmpty() ? user.login : user.displayName;
            auto *item = new QListWidgetItem(
                QStringLiteral("%1\n@%2").arg(name, user.login), m_list);
            item->setData(Qt::UserRole, user.userId);
        }
        m_status->setText(m_blocked.isEmpty() ? tr("Nobody is blocked.")
                                              : tr("Blocked: %1").arg(m_blocked.size()));
    });
}

void BlockListDialog::block()
{
    if (!m_service) {
        return;
    }
    const QString login = m_login->text().trimmed();
    if (login.isEmpty()) {
        m_status->setText(tr("Enter the exact login of the user to block."));
        return;
    }
    m_block->setEnabled(false);
    QPointer<BlockListDialog> self(this);
    m_service->block(login, [this, self](Result<bool> result) {
        if (!self) {
            return;
        }
        m_block->setEnabled(true);
        if (result.isError()) {
            m_status->setText(tr("Could not block: %1").arg(result.error()));
            return;
        }
        m_login->clear();
        reload();
    });
}

void BlockListDialog::unblockSelected()
{
    if (!m_service) {
        return;
    }
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const QString userId = item->data(Qt::UserRole).toString();
    m_unblock->setEnabled(false);
    QPointer<BlockListDialog> self(this);
    m_service->unblock(userId, [this, self](Result<bool> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            m_unblock->setEnabled(true);
            m_status->setText(tr("Could not unblock: %1").arg(result.error()));
            return;
        }
        reload();
    });
}

} // namespace mini
