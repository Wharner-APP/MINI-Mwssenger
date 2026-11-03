#include "services/FolderService.h"

#include "core/Clock.h"
#include "storage/Database.h"
#include "storage/FoldersRepo.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QSet>

namespace mini::services {

namespace {

template <typename T>
void watch(QObject *owner, QFuture<T> future, std::function<void(T)> done)
{
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
                         T value = watcher->result();
                         watcher->deleteLater();
                         done(std::move(value));
                     });
    watcher->setFuture(future);
}

bool validChatType(const QString &type)
{
    return type == QLatin1String("direct") || type == QLatin1String("group")
        || type == QLatin1String("channel") || type == QLatin1String("bot");
}

bool validTypes(const QStringList &types)
{
    QSet<QString> seen;
    for (const QString &type : types) {
        if (!validChatType(type) || seen.contains(type)) {
            return false;
        }
        seen.insert(type);
    }
    return true;
}

} // namespace

FolderService::FolderService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

Result<ChatFolder> FolderService::validate(const ChatFolder &folder)
{
    ChatFolder clean = folder;
    clean.title = folder.title.trimmed();
    clean.nameContains = folder.nameContains.trimmed();
    if (clean.title.isEmpty() || clean.title.size() > 64) {
        return Result<ChatFolder>::error(QStringLiteral("folder.title_invalid"));
    }
    if (clean.nameContains.size() > 64) {
        return Result<ChatFolder>::error(QStringLiteral("folder.name_too_long"));
    }
    if (clean.icon.isEmpty()) {
        clean.icon = QStringLiteral("folder");
    }
    if (!validTypes(clean.includeTypes)) {
        return Result<ChatFolder>::error(QStringLiteral("folder.types_invalid"));
    }
    if (!validTypes(clean.excludeTypes)) {
        return Result<ChatFolder>::error(QStringLiteral("folder.types_invalid"));
    }
    if (clean.onlyUnread && clean.onlyMuted) {
        // The rules would still work, but no chat is both unread and muted
        // with the same intent; reject early instead of showing an empty tab.
        return Result<ChatFolder>::error(QStringLiteral("folder.rules_conflict"));
    }
    return Result<ChatFolder>::ok(clean);
}

void FolderService::listFolders(Done<QVector<ChatFolder>> done)
{
    if (!m_storage) {
        done(Result<QVector<ChatFolder>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([](storage::Database &db) {
        return storage::FoldersRepo(db).list();
    });
    watch<Result<QVector<ChatFolder>>>(
        this, future, [done = std::move(done)](Result<QVector<ChatFolder>> result) {
            done(std::move(result));
        });
}

void FolderService::saveFolder(const ChatFolder &folder, Done<ChatFolder> done)
{
    if (!m_storage) {
        done(Result<ChatFolder>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto valid = validate(folder);
    if (valid.isError()) {
        done(Result<ChatFolder>::error(valid.error()));
        return;
    }
    ChatFolder clean = valid.value();
    if (clean.id.isEmpty()) {
        clean.id = QStringLiteral("f_") + Ids::clientMsgId().left(8);
    }
    auto future = m_storage->submit([clean](storage::Database &db) {
        storage::FoldersRepo repo(db);
        auto existing = repo.list();
        if (existing.isError()) {
            return Result<ChatFolder>::error(existing.error());
        }
        bool found = false;
        for (const ChatFolder &candidate : existing.value()) {
            if (candidate.id == clean.id) {
                found = true;
                break;
            }
        }
        if (!found && existing.value().size() >= kMaxFolders) {
            return Result<ChatFolder>::error(QStringLiteral("folder.limit"));
        }
        ChatFolder stored = clean;
        if (!found) {
            stored.position = existing.value().size();
        }
        auto saved = repo.upsert(stored);
        if (saved.isError()) {
            return Result<ChatFolder>::error(saved.error());
        }
        return Result<ChatFolder>::ok(stored);
    });
    watch<Result<ChatFolder>>(this, future,
                              [this, done = std::move(done)](Result<ChatFolder> result) {
                                  if (result.isOk()) {
                                      emit foldersChanged();
                                  }
                                  done(std::move(result));
                              });
}

void FolderService::removeFolder(const QString &folderId, Done<bool> done)
{
    if (!m_storage || folderId.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("folder.invalid")));
        return;
    }
    auto future = m_storage->submit([folderId](storage::Database &db) {
        return storage::FoldersRepo(db).remove(folderId);
    });
    watch<Result<bool>>(this, future, [this, done = std::move(done)](Result<bool> result) {
        if (result.isOk()) {
            emit foldersChanged();
        }
        done(std::move(result));
    });
}

void FolderService::reorder(const QStringList &folderIds, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([folderIds](storage::Database &db) {
        return storage::FoldersRepo(db).reorder(folderIds);
    });
    watch<Result<bool>>(this, future, [this, done = std::move(done)](Result<bool> result) {
        if (result.isOk()) {
            emit foldersChanged();
        }
        done(std::move(result));
    });
}

} // namespace mini::services
