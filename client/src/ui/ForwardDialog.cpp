#include "ui/ForwardDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace mini {

namespace {
constexpr int kChatIdRole = Qt::UserRole + 1;
}

ForwardDialog::ForwardDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("forwardDialog"));
    setWindowTitle(tr("Forward"));
    resize(380, 460);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("forwardSearchField"));
    m_search->setPlaceholderText(tr("Search chats"));
    m_search->setClearButtonEnabled(true);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("forwardChatList"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Forward"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("forwardConfirm"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(new QLabel(tr("Send to"), this));
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_list, &QListWidget::itemChanged, this, [this, buttons](QListWidgetItem *) {
        bool any = false;
        auto *list = qobject_cast<QListWidget *>(sender());
        if (list) {
            for (int row = 0; row < list->count(); ++row) {
                if (list->item(row)->checkState() == Qt::Checked) {
                    any = true;
                    break;
                }
            }
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(any);
    });
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

void ForwardDialog::setChats(const QVector<Chat> &chats)
{
    m_chats = chats;
    rebuild();
}

void ForwardDialog::rebuild()
{
    const QString filter = m_search->text().trimmed();
    m_list->clear();
    for (const Chat &chat : std::as_const(m_chats)) {
        const QString title = chat.title.isEmpty() ? chat.peerLogin : chat.title;
        if (!filter.isEmpty() && !title.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto *item = new QListWidgetItem(title, m_list);
        item->setData(kChatIdRole, chat.chatId);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
}

QStringList ForwardDialog::selectedChatIds() const
{
    QStringList ids;
    for (int row = 0; row < m_list->count(); ++row) {
        const QListWidgetItem *item = m_list->item(row);
        if (item->checkState() == Qt::Checked) {
            ids.append(item->data(kChatIdRole).toString());
        }
    }
    return ids;
}

} // namespace mini
