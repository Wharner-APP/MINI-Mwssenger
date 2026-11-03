#include "services/SearchService.h"

#include "storage/ChatsRepo.h"
#include "storage/ContactsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>

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

} // namespace

SearchService::SearchService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void SearchService::search(const QString &query, Done<SearchResults> done)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        done(Result<SearchResults>::ok({}));
        return;
    }
    if (!m_storage) {
        done(Result<SearchResults>::error(QStringLiteral("storage_unavailable")));
        return;
    }

    auto future = m_storage->submit([needle](storage::Database &db) {
        SearchResults results;

        auto chats = storage::ChatsRepo(db).list();
        if (chats.isOk()) {
            for (const Chat &chat : chats.value()) {
                if (results.chats.size() >= kMaxPerGroup) {
                    break;
                }
                if (chat.title.contains(needle, Qt::CaseInsensitive)
                    || chat.peerLogin.contains(needle, Qt::CaseInsensitive)) {
                    results.chats.append(chat);
                }
            }
        }

        auto messages = storage::MessagesRepo(db).search(needle, kMaxPerGroup);
        if (messages.isOk()) {
            results.messages = messages.value().mid(0, kMaxPerGroup);
        }

        auto contacts = storage::ContactsRepo(db).list();
        if (contacts.isOk()) {
            for (const Contact &contact : contacts.value()) {
                if (results.people.size() >= kMaxPerGroup) {
                    break;
                }
                if (contact.login.contains(needle, Qt::CaseInsensitive)
                    || contact.displayName.contains(needle, Qt::CaseInsensitive)) {
                    results.people.append(contact);
                }
            }
        }
        return results;
    });
    watch<SearchResults>(this, future, [done = std::move(done)](SearchResults results) {
        done(Result<SearchResults>::ok(std::move(results)));
    });
}

} // namespace mini::services
