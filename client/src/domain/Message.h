#pragma once

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
    QString body;
    QString replyToClientMsgId;
    MessageState state = MessageState::Queued;
    bool outgoing = true;
    qint64 localTs = 0;
    qint64 serverTs = 0;
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
