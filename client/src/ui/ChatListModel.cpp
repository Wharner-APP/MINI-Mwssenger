#include "ui/ChatListModel.h"

#include <QCoreApplication>
#include <QStringList>

namespace mini {

ChatListModel::ChatListModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visible.size();
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return {};
    }
    const ChatItem &item = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case IdRole:
        return item.id;
    case TitleRole:
        return item.title;
    case PreviewRole:
        return item.preview;
    case TimeRole:
        return item.time;
    case UnreadRole:
        return item.unread;
    case PinnedRole:
        return item.pinned;
    case MutedRole:
        return item.muted;
    case OnlineRole:
        return item.online;
    case InitialsRole:
        return item.initials;
    case Qt::DisplayRole:
        return item.title;
    default:
        return {};
    }
}

Qt::ItemFlags ChatListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ChatListModel::setItems(const QVector<ChatItem> &items)
{
    beginResetModel();
    m_all = items;
    rebuildVisible();
    endResetModel();
}

void ChatListModel::setStubCount(int count)
{
    QVector<ChatItem> items;
    items.reserve(qMax(0, count));
    for (int i = 0; i < count; ++i) {
        ChatItem item;
        item.id = QStringLiteral("stub-%1").arg(i);
        item.title = QCoreApplication::translate("ChatListModel", "Chat %1").arg(i + 1);
        item.preview = QCoreApplication::translate("ChatListModel", "Stub message %1").arg(i + 1);
        item.time = QStringLiteral("%1:%2")
                        .arg(9 + (i / 60) % 13, 2, 10, QLatin1Char('0'))
                        .arg(i % 60, 2, 10, QLatin1Char('0'));
        const QStringList words = item.title.split(QLatin1Char(' '));
        item.initials = words.isEmpty() ? QStringLiteral("C")
                                        : words.last().left(1).toUpper();
        item.unread = (i % 7 == 0) ? (i % 5) + 1 : 0;
        item.pinned = (i % 23 == 0);
        item.muted = (i % 11 == 0);
        item.online = (i % 3 == 0);
        items.append(item);
    }
    setItems(items);
}

void ChatListModel::setFilter(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_filter) {
        return;
    }
    beginResetModel();
    m_filter = trimmed;
    rebuildVisible();
    endResetModel();
}

ChatItem ChatListModel::itemAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return {};
    }
    return m_all.at(m_visible.at(row));
}

void ChatListModel::rebuildVisible()
{
    m_visible.clear();
    m_visible.reserve(m_all.size());
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_filter.isEmpty()
            || m_all.at(i).title.contains(m_filter, Qt::CaseInsensitive)
            || m_all.at(i).preview.contains(m_filter, Qt::CaseInsensitive)) {
            m_visible.append(i);
        }
    }
}

} // namespace mini
