#include "domain/ChatFolder.h"

namespace mini {

namespace {

bool containsFold(const QStringList &values, const QString &value)
{
    for (const QString &candidate : values) {
        if (candidate.compare(value, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool folderMatches(const ChatFolder &folder, const Chat &chat)
{
    if (containsFold(folder.excludeChats, chat.chatId)) {
        return false;
    }
    if (containsFold(folder.excludeTypes, chat.kind)) {
        return false;
    }
    if (!folder.includeTypes.isEmpty() && !containsFold(folder.includeTypes, chat.kind)) {
        return false;
    }
    if (folder.onlyUnread && chat.unreadCount <= 0) {
        return false;
    }
    if (folder.onlyMuted && !chat.muted) {
        return false;
    }
    if (!folder.nameContains.trimmed().isEmpty()) {
        const QString needle = folder.nameContains.trimmed();
        if (!chat.title.contains(needle, Qt::CaseInsensitive)
            && !chat.peerLogin.contains(needle, Qt::CaseInsensitive)) {
            return false;
        }
    }
    // An explicit include list narrows the result instead of widening it, so
    // a folder with only chats behaves like "Custom" rather than "everything".
    if (!folder.includeChats.isEmpty() && !containsFold(folder.includeChats, chat.chatId)) {
        return false;
    }
    return true;
}

} // namespace mini
