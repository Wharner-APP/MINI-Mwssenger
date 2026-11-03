#pragma once

#include "core/Result.h"
#include "domain/Chat.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

// Chat-list operations: loading, direct chats by exact login, pin/mute,
// deleting with or without local history, unread counts and drafts.
class ChatService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit ChatService(storage::StorageWorker *storage, QObject *parent = nullptr);

    // The signed-in login. A direct chat is keyed by both logins.
    void setSelfLogin(const QString &login);
    [[nodiscard]] QString selfLogin() const { return m_selfLogin; }

    // Same rules as the server: ^[a-zA-Z][a-zA-Z0-9_]{3,23}$, no reserved
    // names. Returns the login as typed.
    [[nodiscard]] static Result<QString> validatePeerLogin(const QString &login);

    // Stable id for a pair of logins; order and letter case do not matter.
    [[nodiscard]] static QString directChatId(const QString &loginA, const QString &loginB);

    void listChats(Done<QVector<Chat>> done);

    // Creates the chat row on first use and returns the existing one later.
    void openDirectChat(const QString &peerLogin, Done<Chat> done);

    void setPinned(const QString &chatId, bool pinned, Done<bool> done);
    void setMuted(const QString &chatId, bool muted, Done<bool> done);
    void markRead(const QString &chatId, Done<int> done);

    // Exists locally either way; deleteHistory also removes every message.
    void deleteChat(const QString &chatId, bool deleteHistory, Done<bool> done);
    // Removes messages but keeps the chat in the list.
    void clearHistory(const QString &chatId, Done<bool> done);

    // Drafts are per chat and survive a restart. Stored encrypted, never as
    // plain text on disk.
    void saveDraft(const QString &chatId, const QString &text, Done<bool> done);
    void loadDraft(const QString &chatId, Done<QString> done);
    void clearDraft(const QString &chatId, Done<bool> done);

signals:
    void chatsChanged();
    void chatUpdated(const QString &chatId);
    void chatRemoved(const QString &chatId);

private:
    storage::StorageWorker *m_storage = nullptr;
    QString m_selfLogin;
};

} // namespace mini::services
