#pragma once

#include "core/Result.h"
#include "domain/OutboxItem.h"

#include <QString>

#include <optional>

namespace mini::storage {

class Database;

// Outgoing messages before server acknowledgement. Rows are ordered by their
// auto-increment id, so per-chat order survives a restart.
class OutboxRepo
{
public:
    explicit OutboxRepo(Database &db);

    Result<qint64> enqueue(const QString &chatId, const QString &clientMsgId,
                           qint64 createdAt = 0);
    Result<std::optional<OutboxItem>> next(const QString &chatId) const;
    Result<bool> markSent(qint64 id);
    Result<bool> retryLater(qint64 id, qint64 delayMs);
    Result<bool> remove(qint64 id);
    Result<bool> removeByClientMsgId(const QString &clientMsgId);

private:
    Database &m_db;
};

} // namespace mini::storage
