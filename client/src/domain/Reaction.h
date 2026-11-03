#pragma once

#include <QString>
#include <QStringList>

namespace mini {

// §38.1: one reaction per user per message. Repeated press removes it; a
// different emoji replaces the previous one.
struct Reaction
{
    QString clientMsgId;
    QString chatId;
    QString senderLogin;
    QString emoji;
    qint64 createdAt = 0;
};

// The twelve default reactions in the order the picker shows them.
[[nodiscard]] const QStringList &defaultReactions();
// Groups allow at most 20 distinct reactions per message (§38.1).
constexpr int kMaxDistinctReactions = 20;

} // namespace mini
