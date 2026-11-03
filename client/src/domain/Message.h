#pragma once

#include "domain/Media.h"

#include <QString>

namespace mini {

// §4.8: queued, sending, sent, delivered, read, failed, received, deleted_local.
enum class MessageState {
    Queued,
    Sending,
    Sent,
    Delivered,
    Read,
    Failed,
    Received,
    DeletedLocal
};

[[nodiscard]] QString messageStateToString(MessageState state);
[[nodiscard]] MessageState messageStateFromString(const QString &value);

struct Message
{
    QString clientMsgId;
    QString chatId;
    QString serverMsgId;
    QString senderLogin;
    QString body; // text, or the caption of a media message
    QString replyToClientMsgId;
    MediaKind kind = MediaKind::Text;
    QString mediaId; // empty for text; otherwise the media row key
    QString mediaName;
    qint64 mediaSize = 0;
    qint64 mediaDurationMs = 0;
    MessageState state = MessageState::Queued;
    bool outgoing = true;
    qint64 localTs = 0;
    qint64 serverTs = 0;
    // §37.3: non-zero once the body has been edited; the bubble shows the
    // "edited" label.
    qint64 editedAt = 0;
    // §37.2: original author carried by a forwarded copy.
    QString forwardedFrom;
    // §38.2: set by the pins query; not stored in the messages row.
    qint64 pinnedAt = 0;
};

// Cursor for paged history. The timestamp is server_ts once the message is
// confirmed and local_ts before that (§4.8).
struct MessageCursor
{
    qint64 ts = 0;
    QString clientMsgId;

    [[nodiscard]] bool isValid() const { return ts > 0 || !clientMsgId.isEmpty(); }
};

} // namespace mini
