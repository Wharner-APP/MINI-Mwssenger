#pragma once

#include "core/Result.h"
#include "domain/ChatFolder.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace mini::storage {

class Database;

// §48: folders are local rules. Order is persisted so drag-and-drop survives
// a restart; "All" is virtual and never stored.
class FoldersRepo
{
public:
    explicit FoldersRepo(Database &db);

    Result<QVector<ChatFolder>> list() const;
    Result<bool> upsert(const ChatFolder &folder);
    Result<bool> remove(const QString &folderId);
    Result<bool> reorder(const QStringList &folderIds);
    Result<int> count() const;

private:
    Database &m_db;
};

} // namespace mini::storage
