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
    // §39: 0 means the timer is off; otherwise 60 000 .. 604 800 000 ms.
    qint64 expiresAfterMs = 0;
    // §29.14 / §44: a bot chat is marked in the list and the header, and the
    // UI must say it is not end-to-end encrypted. Derived from `_bot` logins
    // on read, so the label survives a restart without a schema change.
    bool isBot = false;
    bool e2e = true;
};

} // namespace mini
