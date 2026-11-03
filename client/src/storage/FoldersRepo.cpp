#include "storage/FoldersRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

#include <QDateTime>

namespace mini::storage {
namespace {

constexpr auto kColumns =
    "folder_id, title, icon, include_types, include_chats, exclude_types, "
    "exclude_chats, name_contains, only_unread, only_muted, position";

QString encodeList(const QStringList &values)
{
    return values.join(QLatin1Char('\n'));
}

QStringList decodeList(const QString &value)
{
    return value.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

ChatFolder readFolder(const detail::Statement &statement)
{
    ChatFolder folder;
    folder.id = statement.text(0);
    folder.title = statement.text(1);
    folder.icon = statement.text(2);
    folder.includeTypes = decodeList(statement.text(3));
    folder.includeChats = decodeList(statement.text(4));
    folder.excludeTypes = decodeList(statement.text(5));
    folder.excludeChats = decodeList(statement.text(6));
    folder.nameContains = statement.text(7);
    folder.onlyUnread = statement.boolean(8);
    folder.onlyMuted = statement.boolean(9);
    folder.position = statement.integer(10);
    return folder;
}

} // namespace

FoldersRepo::FoldersRepo(Database &db) : m_db(db) {}

Result<QVector<ChatFolder>> FoldersRepo::list() const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM chat_folders ORDER BY position ASC, folder_id ASC")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<ChatFolder>>::error(
            detail::sqlError(m_db.handle(), "folders.list"));
    }
    QVector<ChatFolder> folders;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        folders.push_back(readFolder(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<ChatFolder>>::error(
            detail::sqlError(m_db.handle(), "folders.list"));
    }
    return Result<QVector<ChatFolder>>::ok(folders);
}

Result<bool> FoldersRepo::upsert(const ChatFolder &folder)
{
    if (folder.id.isEmpty() || folder.title.trimmed().isEmpty()) {
        return Result<bool>::error(QStringLiteral("folder.invalid"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO chat_folders(folder_id, title, icon, include_types, include_chats,
                                       exclude_types, exclude_chats, name_contains,
                                       only_unread, only_muted, position, updated_at)
              VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
              ON CONFLICT(folder_id) DO UPDATE SET
                  title = excluded.title,
                  icon = excluded.icon,
                  include_types = excluded.include_types,
                  include_chats = excluded.include_chats,
                  exclude_types = excluded.exclude_types,
                  exclude_chats = excluded.exclude_chats,
                  name_contains = excluded.name_contains,
                  only_unread = excluded.only_unread,
                  only_muted = excluded.only_muted,
                  position = excluded.position,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "folders.upsert"));
    }
    statement.bindText(1, folder.id);
    statement.bindText(2, folder.title.trimmed());
    statement.bindText(3, folder.icon);
    statement.bindText(4, encodeList(folder.includeTypes));
    statement.bindText(5, encodeList(folder.includeChats));
    statement.bindText(6, encodeList(folder.excludeTypes));
    statement.bindText(7, encodeList(folder.excludeChats));
    statement.bindText(8, folder.nameContains);
    statement.bindBool(9, folder.onlyUnread);
    statement.bindBool(10, folder.onlyMuted);
    statement.bindInt(11, folder.position);
    statement.bindInt64(12, QDateTime::currentMSecsSinceEpoch());
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "folders.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<bool> FoldersRepo::remove(const QString &folderId)
{
    detail::Statement statement(m_db.handle(),
                                "DELETE FROM chat_folders WHERE folder_id = ?");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "folders.remove"));
    }
    statement.bindText(1, folderId);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "folders.remove"));
    }
    return Result<bool>::ok(sqlite3_changes(m_db.handle()) > 0);
}

Result<bool> FoldersRepo::reorder(const QStringList &folderIds)
{
    if (folderIds.isEmpty()) {
        return Result<bool>::ok(true);
    }
    return m_db.transaction([&](Database &tx) -> Result<bool> {
        detail::Statement statement(tx.handle(),
                                    "UPDATE chat_folders SET position = ? WHERE folder_id = ?");
        if (!statement.isOk()) {
            return Result<bool>::error(detail::sqlError(tx.handle(), "folders.reorder"));
        }
        int position = 0;
        for (const QString &id : folderIds) {
            sqlite3_reset(statement.get());
            sqlite3_clear_bindings(statement.get());
            statement.bindInt(1, position++);
            statement.bindText(2, id);
            if (statement.step() != SQLITE_DONE) {
                return Result<bool>::error(detail::sqlError(tx.handle(), "folders.reorder"));
            }
        }
        return Result<bool>::ok(true);
    });
}

Result<int> FoldersRepo::count() const
{
    detail::Statement statement(m_db.handle(), "SELECT COUNT(*) FROM chat_folders");
    if (!statement.isOk()) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "folders.count"));
    }
    if (statement.step() != SQLITE_ROW) {
        return Result<int>::error(detail::sqlError(m_db.handle(), "folders.count"));
    }
    return Result<int>::ok(statement.integer(0));
}

} // namespace mini::storage
