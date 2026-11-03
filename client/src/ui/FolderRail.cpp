#include "ui/FolderRail.h"

#include "ui/Theme.h"

#include <QAction>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>

namespace mini {

namespace {
constexpr int kFolderIdRole = Qt::UserRole + 1;
}

FolderRail::FolderRail(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("folderRail"));
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("folderRailList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Snap);
    m_list->setUniformItemSizes(true);
    m_list->setFixedHeight(58);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSpacing(2);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                if (m_updating || !current) {
                    return;
                }
                m_activeId = current->data(kFolderIdRole).toString();
                emit folderSelected(m_activeId);
            });
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &FolderRail::showMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this,
            [this] { emitOrder(); });
}

void FolderRail::setFolders(const QVector<ChatFolder> &folders,
                            const QHash<QString, int> &unreadCounts,
                            const QString &activeId)
{
    m_updating = true;
    m_list->clear();
    m_activeId = activeId;

    auto makeItem = [this](const QString &title, const QString &id, int unread,
                           bool fixed) {
        auto *item = new QListWidgetItem(title, m_list);
        item->setData(kFolderIdRole, id);
        item->setSizeHint(QSize(86, 50));
        item->setToolTip(unread > 0 ? QCoreApplication::translate(
                                          "FolderRail", "%1, %2 unread")
                                          .arg(title)
                                          .arg(unread)
                                    : title);
        if (fixed) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
        if (unread > 0) {
            item->setText(QStringLiteral("%1 · %2").arg(title).arg(unread));
        }
        return item;
    };

    auto *all = makeItem(QCoreApplication::translate("FolderRail", "All"), QString(),
                         0, true);
    Q_UNUSED(all);
    for (const ChatFolder &folder : folders) {
        makeItem(folder.title, folder.id, unreadCounts.value(folder.id, 0), false);
    }

    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(kFolderIdRole).toString() == activeId) {
            m_list->setCurrentRow(row);
            break;
        }
    }
    if (activeId.isEmpty() && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    m_updating = false;
}

void FolderRail::setActive(const QString &folderId)
{
    m_activeId = folderId;
    m_updating = true;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(kFolderIdRole).toString() == folderId) {
            m_list->setCurrentRow(row);
            break;
        }
    }
    m_updating = false;
}

void FolderRail::emitOrder()
{
    if (m_updating) {
        return;
    }
    // "All" must stay in the first position even if a drop landed above it.
    if (m_list->count() > 0 && !m_list->item(0)->data(kFolderIdRole).toString().isEmpty()) {
        for (int row = 1; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(kFolderIdRole).toString().isEmpty()) {
                m_updating = true;
                QListWidgetItem *all = m_list->takeItem(row);
                m_list->insertItem(0, all);
                m_list->setCurrentItem(all);
                m_updating = false;
                break;
            }
        }
    }
    QStringList ids;
    for (int row = 1; row < m_list->count(); ++row) {
        const QString id = m_list->item(row)->data(kFolderIdRole).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    emit foldersReordered(ids);
}

void FolderRail::showMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("folderRailMenu"));
    QAction *add = menu.addAction(tr("New folder…"));
    add->setObjectName(QStringLiteral("folderNewAction"));
    connect(add, &QAction::triggered, this, &FolderRail::newFolderRequested);

    QListWidgetItem *item = m_list->itemAt(pos);
    const QString id = item ? item->data(kFolderIdRole).toString() : QString();
    if (!id.isEmpty()) {
        menu.addSeparator();
        QAction *edit = menu.addAction(tr("Edit folder…"));
        edit->setObjectName(QStringLiteral("folderEditAction"));
        connect(edit, &QAction::triggered, this,
                [this, id] { emit editFolderRequested(id); });
        QAction *remove = menu.addAction(tr("Delete folder"));
        remove->setObjectName(QStringLiteral("folderDeleteAction"));
        connect(remove, &QAction::triggered, this,
                [this, id] { emit deleteFolderRequested(id); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

} // namespace mini
