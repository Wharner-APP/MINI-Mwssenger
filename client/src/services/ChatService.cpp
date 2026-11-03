#include "services/ChatService.h"

#include "core/Clock.h"
#include "services/AuthService.h"
#include "storage/BodyCipher.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/SettingsRepo.h"
#include "storage/Sql.h"
#include "storage/StorageWorker.h"

#include <QCryptographicHash>
#include <QFuture>
#include <QFutureWatcher>
#include <QStringList>

#include <algorithm>

namespace mini::services {

namespace {

template <typename T>
void watch(QObject *owner, QFuture<T> future, std::function<void(T)> done)
{
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
                         T value = watcher->result();
                         watcher->deleteLater();
                         done(std::move(value));
                     });
    watcher->setFuture(future);
}

QByteArray draftAad(const QString &chatId)
{
    return QByteArrayLiteral("mini/draft/v1:") + chatId.toUtf8();
}

QString draftKey(const QString &chatId)
{
    return QStringLiteral("draft/") + chatId;
}

} // namespace

ChatService::ChatService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void ChatService::setSelfLogin(const QString &login)
{
    m_selfLogin = login;
}

Result<QString> ChatService::validatePeerLogin(const QString &login)
{
    return AuthService::validateLogin(login);
}

QString ChatService::directChatId(const QString &loginA, const QString &loginB)
{
    QString first = loginA.trimmed().toLower();
    QString second = loginB.trimmed().toLower();
    if (first > second) {
        std::swap(first, second);
    }
    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("mini/direct/v1:") + first.toUtf8()
                                     + QByteArrayLiteral("\n") + second.toUtf8(),
                                 QCryptographicHash::Sha256);
    return QStringLiteral("direct_") + QString::fromLatin1(digest.left(16).toHex());
}

void ChatService::listChats(Done<QVector<Chat>> done)
{
    if (!m_storage) {
        done(Result<QVector<Chat>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([](storage::Database &db) {
        return storage::ChatsRepo(db).list();
    });
    watch<Result<QVector<Chat>>>(this, future, [done = std::move(done)](Result<QVector<Chat>> result) {
        done(std::move(result));
    });
}

void ChatService::openDirectChat(const QString &peerLogin, Done<Chat> done)
{
    const Result<QString> valid = validatePeerLogin(peerLogin);
    if (valid.isError()) {
        done(Result<Chat>::error(valid.error()));
        return;
    }
    if (m_selfLogin.trimmed().isEmpty()) {
        done(Result<Chat>::error(QStringLiteral("self_login_required")));
        return;
    }
    if (valid.value().compare(m_selfLogin, Qt::CaseInsensitive) == 0) {
        done(Result<Chat>::error(QStringLiteral("peer.is_self")));
        return;
    }
    if (!m_storage) {
        done(Result<Chat>::error(QStringLiteral("storage_unavailable")));
        return;
    }

    Chat chat;
    chat.chatId = directChatId(m_selfLogin, valid.value());
    chat.kind = QStringLiteral("direct");
    chat.peerLogin = valid.value();
    chat.title = valid.value();
    chat.updatedAt = Clock::nowMs();

    auto future = m_storage->submit([chat](storage::Database &db) {
        storage::ChatsRepo repo(db);
        auto existing = repo.get(chat.chatId);
        if (existing.isError()) {
            return Result<Chat>::error(existing.error());
        }
        if (existing.value().has_value()) {
            return Result<Chat>::ok(existing.value().value());
        }
        auto inserted = repo.upsert(chat);
        if (inserted.isError()) {
            return Result<Chat>::error(inserted.error());
        }
        return Result<Chat>::ok(chat);
    });
    watch<Result<Chat>>(this, future, [this, done = std::move(done)](Result<Chat> result) {
        if (result.isOk()) {
            emit chatsChanged();
        }
        done(std::move(result));
    });
}

void ChatService::setPinned(const QString &chatId, bool pinned, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, pinned](storage::Database &db) {
        return storage::ChatsRepo(db).setPinned(chatId, pinned);
    });
    watch<Result<bool>>(this, future, [this, chatId, done = std::move(done)](Result<bool> result) {
        if (result.isOk()) {
            emit chatUpdated(chatId);
            emit chatsChanged();
        }
        done(std::move(result));
    });
}

void ChatService::setMuted(const QString &chatId, bool muted, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, muted](storage::Database &db) {
        return storage::ChatsRepo(db).setMuted(chatId, muted);
    });
    watch<Result<bool>>(this, future, [this, chatId, done = std::move(done)](Result<bool> result) {
        if (result.isOk()) {
            emit chatUpdated(chatId);
            emit chatsChanged();
        }
        done(std::move(result));
    });
}

void ChatService::markRead(const QString &chatId, Done<int> done)
{
    if (!m_storage) {
        done(Result<int>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        return storage::MessagesRepo(db).markRead(chatId);
    });
    watch<Result<int>>(this, future, [this, chatId, done = std::move(done)](Result<int> result) {
        if (result.isOk() && result.value() > 0) {
            emit chatUpdated(chatId);
            emit chatsChanged();
        }
        done(std::move(result));
    });
}

void ChatService::deleteChat(const QString &chatId, bool deleteHistory, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, deleteHistory](storage::Database &db) {
        storage::ChatsRepo repo(db);
        const bool removed = deleteHistory ? repo.remove(chatId).value()
                                           : repo.removeKeepingMessages(chatId).value();
        storage::SettingsRepo(db).remove(draftKey(chatId));
        return removed;
    });
    watch<bool>(this, future, [this, chatId, done = std::move(done)](bool removed) {
        if (removed) {
            emit chatRemoved(chatId);
            emit chatsChanged();
        }
        done(Result<bool>::ok(removed));
    });
}

void ChatService::clearHistory(const QString &chatId, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        storage::ChatsRepo repo(db);
        auto existing = repo.get(chatId);
        if (existing.isError() || !existing.value().has_value()) {
            return false;
        }
        Chat cleared = existing.value().value();
        if (repo.remove(chatId).isError()) {
            return false;
        }
        cleared.lastMessageTs = 0;
        cleared.lastMessagePreview.clear();
        cleared.unreadCount = 0;
        cleared.updatedAt = Clock::nowMs();
        storage::SettingsRepo(db).remove(draftKey(chatId));
        return repo.upsert(cleared).isOk();
    });
    watch<bool>(this, future, [this, chatId, done = std::move(done)](bool cleared) {
        if (cleared) {
            emit chatUpdated(chatId);
            emit chatsChanged();
        }
        done(Result<bool>::ok(cleared));
    });
}

void ChatService::saveDraft(const QString &chatId, const QString &text, Done<bool> done)
{
    if (text.isEmpty()) {
        clearDraft(chatId, std::move(done));
        return;
    }
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, text](storage::Database &db) {
        storage::BodyCipher cipher(db.dbKey());
        auto sealed = cipher.encrypt(text, draftAad(chatId));
        if (sealed.isError()) {
            return false;
        }
        const QString encoded = QString::fromLatin1(sealed.value().toBase64());
        return storage::SettingsRepo(db).set(draftKey(chatId), encoded).isOk();
    });
    watch<bool>(this, future, [done = std::move(done)](bool saved) {
        done(Result<bool>::ok(saved));
    });
}

void ChatService::loadDraft(const QString &chatId, Done<QString> done)
{
    if (!m_storage) {
        done(Result<QString>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        auto stored = storage::SettingsRepo(db).get(draftKey(chatId), QString());
        if (stored.isError() || stored.value().isEmpty()) {
            return QString();
        }
        storage::BodyCipher cipher(db.dbKey());
        auto plain = cipher.decrypt(QByteArray::fromBase64(stored.value().toLatin1()),
                                    draftAad(chatId));
        return plain.isOk() ? plain.value() : QString();
    });
    watch<QString>(this, future, [done = std::move(done)](QString text) {
        done(Result<QString>::ok(text));
    });
}

void ChatService::clearDraft(const QString &chatId, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        return storage::SettingsRepo(db).remove(draftKey(chatId)).isOk();
    });
    watch<bool>(this, future, [done = std::move(done)](bool cleared) {
        done(Result<bool>::ok(cleared));
    });
}

} // namespace mini::services
