#pragma once

#include "core/Result.h"
#include "domain/Chat.h"
#include "domain/Contact.h"
#include "domain/Message.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

struct SearchResults
{
    QVector<Chat> chats;
    QVector<Message> messages;
    QVector<Contact> people;
};

// Global Ctrl+K search. The message index is the in-memory FTS table rebuilt
// by MessagesRepo at startup, so nothing plaintext is written to disk (§4.8).
class SearchService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    static constexpr int kMaxPerGroup = 20;

    explicit SearchService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void search(const QString &query, Done<SearchResults> done);

private:
    storage::StorageWorker *m_storage = nullptr;
};

} // namespace mini::services
