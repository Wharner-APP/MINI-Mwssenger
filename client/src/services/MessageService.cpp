#include "services/MessageService.h"

#include "core/Clock.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "services/ChatService.h"
#include "storage/BodyCipher.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/OutboxRepo.h"
#include "storage/Sql.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QPair>

#include <utility>

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

// Order of the outgoing lifecycle; receipts must never move it backwards.
int stateRank(MessageState state)
{
    switch (state) {
    case MessageState::Queued:
        return 0;
    case MessageState::Sending:
        return 1;
    case MessageState::Sent:
        return 2;
    case MessageState::Delivered:
        return 3;
    case MessageState::Read:
        return 4;
    case MessageState::Failed:
    case MessageState::Received:
    case MessageState::DeletedLocal:
        return -1;
    }
    return -1;
}

struct StoredMessage
{
    bool found = false;
    bool bodyOk = true;
    QString peerLogin;
    Message message;
};

StoredMessage loadMessage(storage::Database &db, const QString &clientMsgId)
{
    const auto lookup = storage::MessagesRepo(db).find(clientMsgId);
    StoredMessage stored;
    if (lookup.isError() || !lookup.value().found) {
        return stored;
    }
    stored.found = true;
    stored.bodyOk = lookup.value().bodyOk;
    stored.message = lookup.value().message;
    const auto peer = storage::ChatsRepo(db).peerLogin(stored.message.chatId);
    stored.peerLogin = peer.isOk() ? peer.value() : QString();
    return stored;
}

bool removeOutboxRow(storage::Database &db, const QString &clientMsgId)
{
    return storage::OutboxRepo(db).removeByClientMsgId(clientMsgId).isOk();
}

struct Advance
{
    bool changed = false;
    Message message;
};

Advance advanceMessage(storage::Database &db, const QString &clientMsgId, MessageState target,
                       qint64 serverTs)
{
    StoredMessage stored = loadMessage(db, clientMsgId);
    if (!stored.found) {
        return {};
    }
    if (stateRank(stored.message.state) >= stateRank(target)) {
        return {};
    }
    if (storage::MessagesRepo(db).updateState(clientMsgId, target, serverTs).isError()) {
        return {};
    }
    stored.message.state = target;
    if (serverTs > 0) {
        stored.message.serverTs = serverTs;
    }
    return {true, stored.message};
}

} // namespace

MessageTransport::MessageTransport(QObject *parent) : QObject(parent) {}
MessageTransport::~MessageTransport() = default;

ApiMessageTransport::ApiMessageTransport(net::ApiClient *api, net::WsClient *ws, QObject *parent)
    : MessageTransport(parent), m_api(api), m_ws(ws), m_pullTimer(new QTimer(this))
{
    m_pullTimer->setInterval(30000);
    connect(m_pullTimer, &QTimer::timeout, this, &ApiMessageTransport::pullNow);
    if (m_ws) {
        connect(m_ws, &net::WsClient::messageReceived, this,
                &MessageTransport::messageReceived);
        connect(m_ws, &net::WsClient::ackReceived, this, &MessageTransport::acknowledged);
        connect(m_ws, &net::WsClient::delivery, this, &MessageTransport::delivered);
        connect(m_ws, &net::WsClient::read, this, &MessageTransport::readReceipt);
        connect(m_ws, &net::WsClient::connected, this, [this] {
            emit onlineChanged(true);
            pullNow();
        });
        connect(m_ws, &net::WsClient::disconnected, this,
                [this] { emit onlineChanged(false); });
    }
}

void ApiMessageTransport::setDeviceId(const QString &deviceId)
{
    m_deviceId = deviceId;
    if (!m_deviceId.isEmpty() && !m_pullTimer->isActive()) {
        m_pullTimer->start();
    }
}

void ApiMessageTransport::setPullInterval(int milliseconds)
{
    m_pullTimer->setInterval(qMax(1000, milliseconds));
}

void ApiMessageTransport::pullNow()
{
    if (!m_api || m_deviceId.isEmpty() || m_pulling) {
        return;
    }
    m_pulling = true;
    pullBatch(QString(), {});
}

void ApiMessageTransport::pullBatch(const QString &cursor, const QStringList &mailIds)
{
    m_api->pullMail(m_deviceId, cursor,
                    [this, cursor, mailIds](net::ApiResult<net::MailBatch> result) {
                        if (result.isError()) {
                            m_pulling = false;
                            return;
                        }
                        QStringList collected = mailIds;
                        for (const net::MailItem &item : result.value().items) {
                            emit messageReceived(item.envelope);
                            if (!item.mailId.isEmpty()) {
                                collected.append(item.mailId);
                            }
                        }
                        if (result.value().hasMore && !result.value().cursor.isEmpty()
                            && result.value().cursor != cursor) {
                            pullBatch(result.value().cursor, collected);
                            return;
                        }
                        finishPull(collected);
                    });
}

void ApiMessageTransport::finishPull(const QStringList &mailIds)
{
    if (mailIds.isEmpty()) {
        m_pulling = false;
        return;
    }
    m_api->ackMail(mailIds, [this](net::ApiResult<net::AckResult>) { m_pulling = false; });
}

void ApiMessageTransport::send(const QList<net::Envelope> &envelopes, SendCallback done)
{
    if (!m_api) {
        if (done) {
            done(net::ApiResult<net::SendResult>::fail(
                net::ApiError::network(QStringLiteral("api_unavailable"))));
        }
        return;
    }
    m_api->sendEnvelopes(envelopes, [done = std::move(done)](net::ApiResult<net::SendResult> result) {
        if (done) {
            done(result);
        }
    });
}

bool ApiMessageTransport::isOnline() const
{
    if (m_ws) {
        return m_ws->isConnected();
    }
    return m_api != nullptr;
}

MessageService::MessageService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
    m_pumpTimer.setSingleShot(true);
    connect(&m_pumpTimer, &QTimer::timeout, this, &MessageService::pump);
}

void MessageService::setTransport(MessageTransport *transport)
{
    for (const QMetaObject::Connection &connection : std::as_const(m_transportConnections)) {
        disconnect(connection);
    }
    m_transportConnections.clear();
    m_transport = transport;
    if (!m_transport) {
        return;
    }
    m_transportConnections.append(connect(m_transport, &MessageTransport::messageReceived, this,
                                          &MessageService::handleIncoming));
    m_transportConnections.append(connect(m_transport, &MessageTransport::acknowledged, this,
                                          &MessageService::handleAcknowledged));
    m_transportConnections.append(connect(m_transport, &MessageTransport::delivered, this,
                                          &MessageService::handleDelivered));
    m_transportConnections.append(connect(m_transport, &MessageTransport::readReceipt, this,
                                          &MessageService::handleReadReceipt));
    m_transportConnections.append(connect(m_transport, &MessageTransport::onlineChanged, this,
                                          [this](bool online) {
                                              if (online) {
                                                  flushQueue();
                                              }
                                          }));
}

void MessageService::setCancelWindowMs(int milliseconds)
{
    m_cancelWindowMs = qMax(0, milliseconds);
}

void MessageService::setRetryDelays(const QVector<int> &milliseconds)
{
    m_retryDelays = milliseconds;
}

QString MessageService::sendText(const QString &chatId, const QString &text,
                                 const QString &replyToClientMsgId,
                                 const QString &clientMsgId)
{
    if (chatId.isEmpty() || text.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("empty_message"), QString());
        return {};
    }
    if (!m_storage) {
        emit errorOccurred(QStringLiteral("storage_unavailable"), QString());
        return {};
    }

    Message message;
    message.clientMsgId = clientMsgId.isEmpty() ? Ids::clientMsgId() : clientMsgId;
    message.chatId = chatId;
    message.body = text;
    message.replyToClientMsgId = replyToClientMsgId;
    message.outgoing = true;
    message.state = MessageState::Queued;
    message.localTs = Clock::nowMs();

    m_holdUntil.insert(message.clientMsgId, message.localTs + m_cancelWindowMs);

    const QString id = message.clientMsgId;
    auto future = m_storage->submit([message](storage::Database &db) {
        auto inserted = storage::MessagesRepo(db).insertQueued(message);
        if (inserted.isError()) {
            return -1;
        }
        if (!inserted.value()) {
            return 0; // duplicate client_msg_id: nothing new to send
        }
        auto enqueued = storage::OutboxRepo(db).enqueue(message.chatId, message.clientMsgId,
                                                        message.localTs);
        if (enqueued.isError()) {
            return -1;
        }
        auto touched = storage::ChatsRepo(db).touchLastMessage(
            message.chatId, message.localTs, message.body, false);
        if (touched.isError()) {
            return -1;
        }
        return 1;
    });
    watch<int>(this, future, [this, message](int stored) {
        if (stored < 0) {
            m_holdUntil.remove(message.clientMsgId);
            emit errorOccurred(QStringLiteral("storage_failed"), QString());
            return;
        }
        if (stored == 0) {
            m_holdUntil.remove(message.clientMsgId);
            return;
        }
        emit messageAdded(message);
        schedulePump(m_cancelWindowMs > 0 ? m_cancelWindowMs : 0);
    });
    return id;
}

void MessageService::cancelSend(const QString &clientMsgId, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    const int window = m_cancelWindowMs;
    auto future = m_storage->submit([clientMsgId, window](storage::Database &db) {
        StoredMessage stored = loadMessage(db, clientMsgId);
        if (!stored.found || stored.message.state != MessageState::Queued) {
            return QPair<int, QString>(0, QString());
        }
        if (Clock::nowMs() - stored.message.localTs > window) {
            return QPair<int, QString>(0, QString());
        }
        removeOutboxRow(db, clientMsgId);
        storage::MessagesRepo(db).deleteLocal(clientMsgId);
        storage::ChatsRepo(db).recalculateLastMessage(stored.message.chatId);
        return QPair<int, QString>(1, stored.message.chatId);
    });
    watch<QPair<int, QString>>(this, future,
                               [this, clientMsgId, done = std::move(done)](
                                   QPair<int, QString> result) {
        const bool cancelled = result.first == 1;
        if (cancelled) {
            m_holdUntil.remove(clientMsgId);
            emit messageRemoved(clientMsgId, result.second);
            schedulePump(0);
        }
        done(Result<bool>::ok(cancelled));
    });
}

void MessageService::deleteLocal(const QString &clientMsgId, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([clientMsgId](storage::Database &db) {
        const StoredMessage stored = loadMessage(db, clientMsgId);
        if (!stored.found) {
            return QPair<bool, QString>(false, QString());
        }
        removeOutboxRow(db, clientMsgId);
        auto removed = storage::MessagesRepo(db).deleteLocal(clientMsgId);
        if (!removed.isOk() || !removed.value()) {
            return QPair<bool, QString>(false, QString());
        }
        storage::ChatsRepo(db).recalculateLastMessage(stored.message.chatId);
        return QPair<bool, QString>(true, stored.message.chatId);
    });
    watch<QPair<bool, QString>>(
        this, future,
        [this, clientMsgId, done = std::move(done)](QPair<bool, QString> result) {
            if (result.first) {
                m_holdUntil.remove(clientMsgId);
                emit messageRemoved(clientMsgId, result.second);
            }
            done(Result<bool>::ok(result.first));
        });
}

void MessageService::loadHistory(const QString &chatId, const MessageCursor &before, int limit,
                                 Done<QVector<Message>> done)
{
    if (!m_storage) {
        done(Result<QVector<Message>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, before, limit](storage::Database &db) {
        return storage::MessagesRepo(db).listByChat(chatId, before, limit);
    });
    watch<Result<QVector<Message>>>(this, future,
                                    [done = std::move(done)](Result<QVector<Message>> result) {
                                        done(std::move(result));
                                    });
}

void MessageService::markChatRead(const QString &chatId, Done<int> done)
{
    if (!m_storage) {
        done(Result<int>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        return storage::MessagesRepo(db).markRead(chatId);
    });
    watch<Result<int>>(this, future, [done = std::move(done)](Result<int> result) {
        done(std::move(result));
    });
}

void MessageService::flushQueue()
{
    m_pumpTimer.stop();
    pump();
}

void MessageService::schedulePump(int delayMs)
{
    const int delay = qMax(0, delayMs);
    if (m_pumpTimer.isActive() && m_pumpTimer.remainingTime() <= delay) {
        return;
    }
    m_pumpTimer.start(delay);
}

void MessageService::pump()
{
    if (!m_storage || !m_crypto || !m_transport || !m_transport->isOnline()) {
        return;
    }
    auto future = m_storage->submit([](storage::Database &db) {
        QVector<OutboxItem> heads;
        auto chats = storage::ChatsRepo(db).list();
        if (chats.isError()) {
            return heads;
        }
        for (const Chat &chat : chats.value()) {
            auto next = storage::OutboxRepo(db).next(chat.chatId);
            if (next.isOk() && next.value().has_value()) {
                heads.append(next.value().value());
            }
        }
        return heads;
    });
    watch<QVector<OutboxItem>>(this, future, [this](QVector<OutboxItem> heads) {
        const qint64 now = Clock::nowMs();
        qint64 nextHold = 0;
        for (const OutboxItem &item : std::as_const(heads)) {
            auto hold = m_holdUntil.find(item.clientMsgId);
            if (hold != m_holdUntil.end()) {
                if (hold.value() > now) {
                    nextHold = nextHold == 0 ? hold.value() : qMin(nextHold, hold.value());
                    continue;
                }
                m_holdUntil.erase(hold);
            }
            if (m_sendingChats.contains(item.chatId)) {
                continue;
            }
            sendHead(item);
        }
        if (nextHold > now) {
            schedulePump(static_cast<int>(nextHold - now));
        }
        emit pendingChanged(static_cast<int>(heads.size()));
    });
}

void MessageService::sendHead(const OutboxItem &item)
{
    m_sendingChats.insert(item.chatId);
    const QString clientMsgId = item.clientMsgId;
    auto future = m_storage->submit(
        [clientMsgId](storage::Database &db) { return loadMessage(db, clientMsgId); });
    watch<StoredMessage>(this, future, [this, item](StoredMessage stored) {
        if (!stored.found || stored.message.state == MessageState::DeletedLocal
            || stored.message.state == MessageState::Failed
            || stored.message.state == MessageState::Sent
            || stored.message.state == MessageState::Delivered
            || stored.message.state == MessageState::Read) {
            m_sendingChats.remove(item.chatId);
            auto cleanup = m_storage->submit(
                [clientMsgId = item.clientMsgId](storage::Database &db) {
                    return removeOutboxRow(db, clientMsgId);
                });
            watch<bool>(this, cleanup, [this](bool) { schedulePump(0); });
            return;
        }
        if (!stored.bodyOk || !m_crypto) {
            m_sendingChats.remove(item.chatId);
            markFailed(item, QStringLiteral("crypto_unavailable"));
            return;
        }
        if (!m_crypto->isReady(item.chatId)) {
            const QString chatId = item.chatId;
            m_crypto->prepare(chatId, stored.peerLogin,
                              [this, item, chatId](Result<bool> ready) {
                                  m_sendingChats.remove(chatId);
                                  if (ready.isError()) {
                                      markFailed(item, ready.error());
                                      return;
                                  }
                                  schedulePump(0);
                              });
            return;
        }

        QJsonObject header;
        if (!stored.peerLogin.isEmpty()) {
            header.insert(QStringLiteral("to"), stored.peerLogin);
        }
        header.insert(QStringLiteral("ts"), static_cast<double>(Clock::nowMs()));
        auto sealed = m_crypto->encrypt(item.chatId, stored.message.body.toUtf8(), &header);
        if (sealed.isError()) {
            m_sendingChats.remove(item.chatId);
            markFailed(item, QStringLiteral("crypto_failed"));
            return;
        }
        if (!stored.message.replyToClientMsgId.isEmpty()) {
            header.insert(QStringLiteral("reply_to"), stored.message.replyToClientMsgId);
        }

        Message sending = stored.message;
        sending.state = MessageState::Sending;
        const QString id = item.clientMsgId;
        auto marked = m_storage->submit([id](storage::Database &db) {
            return storage::MessagesRepo(db)
                .updateState(id, MessageState::Sending)
                .isOk();
        });
        const net::Envelope envelope{
            QStringLiteral("text"), item.clientMsgId, item.chatId, sealed.value(), header};
        watch<bool>(this, marked,
                   [this, item, sending, envelope](bool) {
                       emit messageUpdated(sending);
                       m_transport->send({envelope},
                                         [this, item](const net::ApiResult<net::SendResult> &result) {
                                             m_sendingChats.remove(item.chatId);
                                             const bool accepted =
                                                 result.isOk()
                                                 && result.value().accepted.contains(
                                                     item.clientMsgId);
                                             if (accepted) {
                                                 const QString id = item.clientMsgId;
                                                 const qint64 outboxId = item.id;
                                                 auto stored = m_storage->submit(
                                                     [id, outboxId](storage::Database &db) {
                                                         storage::MessagesRepo(db).updateState(
                                                             id, MessageState::Sent);
                                                         storage::OutboxRepo(db).remove(outboxId);
                                                         return loadMessage(db, id);
                                                     });
                                                 watch<StoredMessage>(
                                                     this, stored,
                                                     [this](StoredMessage updated) {
                                                         if (updated.found) {
                                                             emit messageUpdated(updated.message);
                                                         }
                                                         schedulePump(0);
                                                     });
                                                 return;
                                             }
                                             if (result.isOk()) {
                                                 markFailed(item,
                                                            QStringLiteral("send_rejected"));
                                                 return;
                                             }
                                             if (result.error().isRetryable()) {
                                                 handleSendFailure(item, result.error());
                                                 return;
                                             }
                                             markFailed(item,
                                                        result.error().code.isEmpty()
                                                            ? QStringLiteral("send_failed")
                                                            : result.error().code);
                                         });
                   });
    });
}

void MessageService::markFailed(const OutboxItem &item, const QString &code)
{
    const QString id = item.clientMsgId;
    const qint64 outboxId = item.id;
    auto future = m_storage->submit([id, outboxId](storage::Database &db) {
        storage::MessagesRepo(db).updateState(id, MessageState::Failed);
        storage::OutboxRepo(db).remove(outboxId);
        return loadMessage(db, id);
    });
    watch<StoredMessage>(this, future, [this, code](StoredMessage stored) {
        if (stored.found) {
            emit messageUpdated(stored.message);
            emit messageSendFailed(stored.message.clientMsgId, stored.message.chatId,
                                   stored.message.body, code);
        }
        emit errorOccurred(code, QString());
        schedulePump(0);
    });
}

void MessageService::handleSendFailure(const OutboxItem &item, const net::ApiError &error)
{
    Q_UNUSED(error);
    const int attempt = item.attempts;
    const int delay = m_retryDelays.isEmpty()
        ? 0
        : m_retryDelays.at(qMin<int>(attempt, static_cast<int>(m_retryDelays.size()) - 1));
    const QString id = item.clientMsgId;
    const qint64 outboxId = item.id;
    auto future = m_storage->submit([id, outboxId, delay](storage::Database &db) {
        storage::OutboxRepo(db).retryLater(outboxId, delay);
        storage::MessagesRepo(db).updateState(id, MessageState::Queued);
        return loadMessage(db, id);
    });
    watch<StoredMessage>(this, future, [this, delay, attempt](StoredMessage stored) {
        if (stored.found) {
            emit messageUpdated(stored.message);
        }
        if (attempt < m_retryDelays.size()) {
            schedulePump(qMax(1, delay));
        }
    });
}

void MessageService::handleIncoming(const net::Envelope &envelope)
{
    if (!m_crypto || !m_storage) {
        return;
    }
    // The server stamps the authenticated sender. A client-supplied name or an
    // arbitrary chat id must never decide which chat the body lands in.
    const QString sender =
        envelope.header.value(QStringLiteral("sender_login")).toString();
    if (sender.isEmpty() || m_selfLogin.isEmpty()) {
        emit errorOccurred(QStringLiteral("sender_unverified"), QString());
        return;
    }
    if (envelope.chatId != ChatService::directChatId(m_selfLogin, sender)) {
        emit errorOccurred(QStringLiteral("chat_id_mismatch"), QString());
        return;
    }
    // The first message carries the X3DH preamble: the responder builds its
    // session inside decrypt(). Preparing first would start the wrong role.
    const bool hasIntro = envelope.header.contains(QStringLiteral("x3dh"));
    if (!m_crypto->isReady(envelope.chatId) && !hasIntro) {
        m_crypto->prepare(envelope.chatId, sender,
                          [this, envelope](Result<bool> ready) {
                              if (ready.isError()) {
                                  emit errorOccurred(QStringLiteral("crypto_not_ready"),
                                                     ready.error());
                                  return;
                              }
                              handleIncoming(envelope);
                          });
        return;
    }
    auto plain = m_crypto->decrypt(envelope.chatId, envelope.ciphertext, envelope.header);
    if (plain.isError()) {
        emit errorOccurred(QStringLiteral("decrypt_failed"), QString());
        return;
    }

    Message message;
    message.clientMsgId = envelope.clientMsgId;
    message.chatId = envelope.chatId;
    message.body = QString::fromUtf8(plain.value());
    message.senderLogin = sender;
    message.replyToClientMsgId =
        envelope.header.value(QStringLiteral("reply_to")).toString();
    message.outgoing = false;
    message.state = MessageState::Received;
    message.localTs = Clock::nowMs();
    const qint64 serverTs =
        static_cast<qint64>(envelope.header.value(QStringLiteral("ts")).toDouble(0));
    message.serverTs = serverTs;

    auto future = m_storage->submit([message](storage::Database &db) {
        // A message may arrive from someone we have never written to. The
        // chat row must exist before the insert, otherwise the FK rejects it
        // and the incoming message is lost.
        storage::ChatsRepo chats(db);
        auto existing = chats.get(message.chatId);
        if (existing.isError()) {
            return -1;
        }
        if (!existing.value().has_value()) {
            Chat chat;
            chat.chatId = message.chatId;
            chat.kind = QStringLiteral("direct");
            chat.peerLogin = message.senderLogin;
            chat.title = message.senderLogin.isEmpty() ? message.chatId
                                                       : message.senderLogin;
            chat.updatedAt = message.serverTs > 0 ? message.serverTs : message.localTs;
            if (chats.upsert(chat).isError()) {
                return -1;
            }
        }

        auto inserted = storage::MessagesRepo(db).insertReceived(message);
        if (inserted.isError()) {
            return -1;
        }
        if (!inserted.value()) {
            return 0;
        }
        auto touched = storage::ChatsRepo(db).touchLastMessage(
            message.chatId, message.serverTs > 0 ? message.serverTs : message.localTs,
            message.body, true);
        return touched.isError() ? -1 : 1;
    });
    watch<int>(this, future, [this, message](int stored) {
        if (stored < 0) {
            emit errorOccurred(QStringLiteral("storage_failed"), QString());
            return;
        }
        if (stored == 1) {
            emit messageAdded(message);
        }
    });
}

void MessageService::handleAcknowledged(const QString &clientMsgId, const QString &chatId)
{
    Q_UNUSED(chatId);
    if (!m_storage) {
        return;
    }
    auto future = m_storage->submit([clientMsgId](storage::Database &db) {
        return advanceMessage(db, clientMsgId, MessageState::Sent, 0);
    });
    watch<Advance>(this, future, [this](Advance advance) {
        if (advance.changed) {
            emit messageUpdated(advance.message);
        }
    });
}

void MessageService::handleDelivered(const QString &clientMsgId, const QString &chatId, qint64 at)
{
    Q_UNUSED(chatId);
    if (!m_storage) {
        return;
    }
    auto future = m_storage->submit([clientMsgId, at](storage::Database &db) {
        return advanceMessage(db, clientMsgId, MessageState::Delivered, at);
    });
    watch<Advance>(this, future, [this](Advance advance) {
        if (advance.changed) {
            emit messageUpdated(advance.message);
        }
    });
}

void MessageService::handleReadReceipt(const QString &chatId, const QStringList &clientMsgIds)
{
    Q_UNUSED(chatId);
    if (!m_storage) {
        return;
    }
    auto future = m_storage->submit([clientMsgIds](storage::Database &db) {
        QVector<Message> changed;
        for (const QString &clientMsgId : clientMsgIds) {
            Advance advance = advanceMessage(db, clientMsgId, MessageState::Read, 0);
            if (advance.changed) {
                changed.append(advance.message);
            }
        }
        return changed;
    });
    watch<QVector<Message>>(this, future, [this](QVector<Message> changed) {
        for (const Message &message : std::as_const(changed)) {
            emit messageUpdated(message);
        }
    });
}

} // namespace mini::services
