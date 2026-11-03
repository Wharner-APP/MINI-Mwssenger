#pragma once

#include <QString>

namespace mini {

// One outgoing message waiting for the server. Order inside a chat follows
// the ascending id, so a restart resumes exactly where the client stopped.
struct OutboxItem
{
    qint64 id = 0;
    QString chatId;
    QString clientMsgId;
    int attempts = 0;
    qint64 createdAt = 0;
    qint64 nextAttemptAt = 0;
};

} // namespace mini
