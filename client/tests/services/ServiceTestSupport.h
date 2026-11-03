#pragma once

#include "domain/Chat.h"
#include "domain/Message.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace mini::services::test {

// 32-byte DB_KEY used only by tests; production keys come from Kdf.
inline QByteArray dbKey()
{
    return QByteArray(32, '\x5a');
}

inline bool seedChat(storage::StorageWorker &worker, const QString &chatId,
                     const QString &peerLogin, qint64 lastTs = 0)
{
    auto future = worker.submit([chatId, peerLogin, lastTs](storage::Database &db) {
        Chat chat;
        chat.chatId = chatId;
        chat.kind = QStringLiteral("direct");
        chat.peerLogin = peerLogin;
        chat.title = peerLogin;
        chat.lastMessageTs = lastTs;
        return storage::ChatsRepo(db).upsert(chat).isOk();
    });
    future.waitForFinished();
    return future.result();
}

inline bool seedMessage(storage::StorageWorker &worker, const QString &chatId,
                        const QString &clientMsgId, const QString &body, qint64 ts,
                        bool outgoing = true)
{
    auto future = worker.submit([=](storage::Database &db) {
        Message message;
        message.clientMsgId = clientMsgId;
        message.chatId = chatId;
        message.body = body;
        message.localTs = ts;
        message.outgoing = outgoing;
        return storage::MessagesRepo(db).insertQueued(message).value();
    });
    future.waitForFinished();
    return future.result();
}

inline std::optional<Message> fetchMessage(storage::StorageWorker &worker, const QString &chatId,
                                           const QString &clientMsgId)
{
    auto future = worker.submit([chatId](storage::Database &db) {
        auto listed = storage::MessagesRepo(db).listByChat(chatId, {}, 500);
        return listed.isOk() ? listed.value() : QVector<Message>();
    });
    future.waitForFinished();
    for (const Message &message : future.result()) {
        if (message.clientMsgId == clientMsgId) {
            return message;
        }
    }
    return std::nullopt;
}

inline int messageCount(storage::StorageWorker &worker, const QString &chatId)
{
    auto future = worker.submit([chatId](storage::Database &db) {
        auto listed = storage::MessagesRepo(db).listByChat(chatId, {}, 500);
        return listed.isOk() ? listed.value().size() : -1;
    });
    future.waitForFinished();
    return future.result();
}

inline std::optional<Chat> fetchChat(storage::StorageWorker &worker, const QString &chatId)
{
    auto future = worker.submit([chatId](storage::Database &db) {
        auto chat = storage::ChatsRepo(db).get(chatId);
        return chat.isOk() ? chat.value() : std::optional<Chat>();
    });
    future.waitForFinished();
    return future.result();
}

inline int chatCount(storage::StorageWorker &worker)
{
    auto future = worker.submit(
        [](storage::Database &db) { return storage::ChatsRepo(db).list().value().size(); });
    future.waitForFinished();
    return future.result();
}

} // namespace mini::services::test
