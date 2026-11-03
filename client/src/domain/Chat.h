#pragma once

#include <QString>

namespace mini {

// Copyable value object handed to the UI. Storage never leaks SQL rows.
struct Chat
{
    QString chatId;
    QString kind = QStringLiteral("direct");
    QString peerLogin;
    QString title;
    qint64 lastMessageTs = 0;
    QString lastMessagePreview;
    int unreadCount = 0;
    bool pinned = false;
    bool muted = false;
    qint64 updatedAt = 0;
    qint64 mutedUntil = 0; // 0 with muted=true means "forever"
    bool archived = false;
};

} // namespace mini
