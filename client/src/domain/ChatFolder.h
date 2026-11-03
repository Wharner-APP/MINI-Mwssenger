#pragma once

#include "domain/Chat.h"

#include <QString>
#include <QStringList>

namespace mini {

// §48: folders are local rules, not server state. A chat may belong to more
// than one folder; "All" always exists and is not stored.
struct ChatFolder
{
    QString id;
    QString title;
    QString icon = QStringLiteral("folder");
    QStringList includeTypes;   // direct, group, channel, bot
    QStringList includeChats;   // explicit chat ids
    QStringList excludeTypes;
    QStringList excludeChats;
    // Rules from §11.3 #39: name, mute and unread.
    QString nameContains;
    bool onlyUnread = false;
    bool onlyMuted = false;
    int position = 0;
};

constexpr int kMaxFolders = 10;
constexpr int kMaxFoldersPremium = 20;

// Pure rule evaluation, shared by the service and the tests.
[[nodiscard]] bool folderMatches(const ChatFolder &folder, const Chat &chat);

} // namespace mini
