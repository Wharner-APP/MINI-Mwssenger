#pragma once

#include "core/Result.h"
#include "domain/ChatFolder.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

// §48: up to ten local folders (twenty with the premium limit), ordered by
// drag-and-drop. The rules are evaluated on the client against the chat list.
class FolderService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit FolderService(storage::StorageWorker *storage, QObject *parent = nullptr);

    [[nodiscard]] static Result<ChatFolder> validate(const ChatFolder &folder);

    void listFolders(Done<QVector<ChatFolder>> done);
    // Creates when the id is empty, otherwise updates. Rejects the eleventh
    // folder with `folder.limit`.
    void saveFolder(const ChatFolder &folder, Done<ChatFolder> done);
    void removeFolder(const QString &folderId, Done<bool> done);
    void reorder(const QStringList &folderIds, Done<bool> done);

signals:
    void foldersChanged();

private:
    storage::StorageWorker *m_storage = nullptr;
};

} // namespace mini::services
