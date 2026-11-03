#include "domain/Message.h"

namespace mini {

QString messageStateToString(MessageState state)
{
    switch (state) {
    case MessageState::Queued:
        return QStringLiteral("queued");
    case MessageState::Sending:
        return QStringLiteral("sending");
    case MessageState::Sent:
        return QStringLiteral("sent");
    case MessageState::Delivered:
        return QStringLiteral("delivered");
    case MessageState::Read:
        return QStringLiteral("read");
    case MessageState::Failed:
        return QStringLiteral("failed");
    case MessageState::Received:
        return QStringLiteral("received");
    case MessageState::DeletedLocal:
        return QStringLiteral("deleted_local");
    }
    return QStringLiteral("queued");
}

MessageState messageStateFromString(const QString &value)
{
    if (value == QStringLiteral("sending")) {
        return MessageState::Sending;
    }
    if (value == QStringLiteral("sent")) {
        return MessageState::Sent;
    }
    if (value == QStringLiteral("delivered")) {
        return MessageState::Delivered;
    }
    if (value == QStringLiteral("read")) {
        return MessageState::Read;
    }
    if (value == QStringLiteral("failed")) {
        return MessageState::Failed;
    }
    if (value == QStringLiteral("received")) {
        return MessageState::Received;
    }
    if (value == QStringLiteral("deleted_local")) {
        return MessageState::DeletedLocal;
    }
    return MessageState::Queued;
}

} // namespace mini
