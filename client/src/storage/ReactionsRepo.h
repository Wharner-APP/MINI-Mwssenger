#pragma once

#include "core/Result.h"
#include "domain/Reaction.h"

#include <QString>
#include <QVector>

namespace mini::storage {

class Database;

// §38.1: reactions are local rows keyed by (message, user). The service
// mirrors each change to the other participants as a control envelope.
class ReactionsRepo
{
public:
    explicit ReactionsRepo(Database &db);

    // One reaction per user: a second emoji replaces the first.
    Result<bool> set(const Reaction &reaction);
    Result<bool> remove(const QString &clientMsgId, const QString &senderLogin);
    Result<bool> removeAll(const QString &clientMsgId);
    Result<QVector<Reaction>> listForChat(const QString &chatId) const;
    Result<QVector<Reaction>> listForMessage(const QString &clientMsgId) const;
    Result<int> distinctCount(const QString &clientMsgId) const;

private:
    Database &m_db;
};

} // namespace mini::storage
