#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace mini {

struct ChatItem
{
    QString id;
    QString title;
    QString preview;
    QString time;
    QString initials;
    int unread = 0;
    bool pinned = false;
    bool muted = false;
    bool online = false;
    bool archived = false;
};

class ChatListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        TimeRole,
        UnreadRole,
        PinnedRole,
        MutedRole,
        OnlineRole,
        InitialsRole,
    };

    explicit ChatListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setItems(const QVector<ChatItem> &items);
    void setStubCount(int count);
    void setFilter(const QString &text);

    [[nodiscard]] ChatItem itemAt(int row) const;
    [[nodiscard]] int totalCount() const { return m_all.size(); }
    [[nodiscard]] QString filter() const { return m_filter; }

private:
    void rebuildVisible();

    QVector<ChatItem> m_all;
    QVector<int> m_visible;
    QString m_filter;
};

} // namespace mini
