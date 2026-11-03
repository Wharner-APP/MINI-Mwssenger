#pragma once

#include "domain/ChatFolder.h"

#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace mini {

// §48.2: horizontal rail of folder icons above the chat list with unread
// counters, an active highlight and drag-and-drop ordering. "All" is always
// first and cannot be moved or deleted.
class FolderRail : public QWidget
{
    Q_OBJECT

public:
    explicit FolderRail(QWidget *parent = nullptr);

    [[nodiscard]] QListWidget *list() const { return m_list; }
    [[nodiscard]] QString activeFolderId() const { return m_activeId; }

    void setFolders(const QVector<ChatFolder> &folders,
                    const QHash<QString, int> &unreadCounts, const QString &activeId);
    void setActive(const QString &folderId);

signals:
    void folderSelected(const QString &folderId);
    void foldersReordered(const QStringList &folderIds);
    void newFolderRequested();
    void editFolderRequested(const QString &folderId);
    void deleteFolderRequested(const QString &folderId);

private:
    void showMenu(const QPoint &pos);
    void emitOrder();

    QListWidget *m_list = nullptr;
    QString m_activeId;
    bool m_updating = false;
};

} // namespace mini
