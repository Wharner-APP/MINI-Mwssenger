#include "services/MessageService.h"

#include "core/Clock.h"
#include "domain/Channel.h"
#include "domain/Disappearing.h"
#include "domain/Group.h"
#include "domain/Media.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "services/ChatService.h"
#include "storage/BodyCipher.h"
#include "storage/ChannelsRepo.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/GroupsRepo.h"
#include "storage/MediaRepo.h"
#include "storage/MessagesRepo.h"
#include "storage/OutboxRepo.h"
#include "storage/PollsRepo.h"
#include "storage/ReactionsRepo.h"
#include "storage/Sql.h"
#include "storage/StorageWorker.h"

#include <QCoreApplication>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>

#include <optional>
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
    bool wireOk = true;
    QString peerLogin;
    Message message;
    QByteArray wire; // plaintext that goes into the envelope
};

MediaWire wireFromRecord(storage::Database &db, const MediaRecord &record)
{
    MediaWire wire;
    wire.kind = record.kind;
    wire.fileId = record.mediaId;
    wire.fileKey = record.fileKey;
    wire.nonceBase = record.nonceBase;
    wire.size = record.size;
    wire.chunkSize = record.chunkSize;
    wire.fileHash = record.fileHash;
    wire.mime = record.mime;
    wire.name = record.name;
    wire.width = record.width;
    wire.height = record.height;
    wire.durationMs = record.durationMs;
    const auto thumb =
        storage::MediaRepo(db).meta(record.mediaId, QStringLiteral("thumb_b64"));
    if (thumb.isOk() && !thumb.value().isEmpty()) {
        wire.thumb = QByteArray::fromBase64(thumb.value().toLatin1());
    }
    return wire;
}

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

    const MediaKind kind = stored.message.kind;
    if (!stored.message.pollId.isEmpty()) {
        // §38.3: the wire form is the poll definition, not the visible
        // question, so recipients can render the options offline.
        const auto poll = storage::PollsRepo(db).get(stored.message.pollId);
        if (poll.isError() || !poll.value().has_value()) {
            stored.wireOk = false;
            return stored;
        }
        PollBody body;
        body.pollId = poll.value()->pollId;
        body.question = poll.value()->question;
        body.options = poll.value()->options;
        body.anonymous = poll.value()->anonymous;
        body.multiple = poll.value()->multiple;
        body.quiz = poll.value()->quiz;
        body.allowChange = poll.value()->allowChange;
        body.correctOption = poll.value()->correctOption;
        stored.wire = encodePollBody(body);
        return stored;
    }
    if (kind == MediaKind::Text) {
        stored.wire = stored.message.body.toUtf8();
    } else if (kind == MediaKind::Sticker) {
        StickerRef sticker;
        sticker.packId = stored.message.mediaId.section(QLatin1Char('/'), 0, 0);
        sticker.stickerId = stored.message.mediaId.section(QLatin1Char('/'), 1, 1);
        sticker.title = stored.message.body;
        if (sticker.packId.isEmpty() || sticker.stickerId.isEmpty()) {
            stored.wireOk = false;
        } else {
            stored.wire = encodeStickerBody(sticker);
        }
    } else if (kind == MediaKind::Gif) {
        GifRef gif;
        gif.gifId = stored.message.mediaId;
        gif.title = stored.message.body;
        if (gif.gifId.isEmpty()) {
            stored.wireOk = false;
        } else {
            stored.wire = encodeGifBody(gif);
        }
    } else if (kind == MediaKind::Album) {
        AlbumBody album;
        album.caption = stored.message.body;
        storage::MediaRepo repo(db);
        const QStringList ids = splitMediaIds(stored.message.mediaId);
        for (const QString &mediaId : ids) {
            const auto media = repo.get(mediaId);
            if (media.isError() || !media.value().has_value()) {
                stored.wireOk = false;
                break;
            }
            album.items.append(wireFromRecord(db, media.value().value()));
        }
        if (stored.wireOk && (album.items.isEmpty() || album.items.size() > 10)) {
            stored.wireOk = false;
        }
        if (stored.wireOk) {
            stored.wire = encodeAlbumBody(album);
        }
    } else {
        const auto media = storage::MediaRepo(db).get(stored.message.mediaId);
        if (media.isError() || !media.value().has_value()) {
            stored.wireOk = false;
        } else {
            stored.wire = encodeMediaBody(stored.message.kind, stored.message.body,
                                          wireFromRecord(db, media.value().value()));
        }
    }
    return stored;
}

// Turns a decrypted envelope body into the local message fields. Sticker and
// GIF bodies only carry catalog ids, so they never create a media row.
struct IncomingRich
{
    MediaKind kind = MediaKind::Text;
    QString body;
    QString mediaId;
    QString mediaName;
    qint64 mediaSize = 0;
    qint64 mediaDurationMs = 0;
    QVector<MediaWire> wires;
    bool hasPoll = false;
    PollBody poll;
};

IncomingRich makeIncomingRich(const QByteArray &plain)
{
    const RichBody rich = decodeRichBody(plain);
    IncomingRich out;
    out.kind = rich.kind;
    out.wires = rich.items;
    if (rich.hasPoll) {
        out.hasPoll = true;
        out.poll = rich.poll;
        out.body = rich.poll.question;
        return out;
    }
    switch (rich.kind) {
    case MediaKind::Text:
        out.body = QString::fromUtf8(plain);
        break;
    case MediaKind::Sticker:
        out.body = rich.sticker.title;
        out.mediaName = rich.sticker.title;
        out.mediaId = rich.sticker.packId + QLatin1Char('/') + rich.sticker.stickerId;
        break;
    case MediaKind::Gif:
        out.body = rich.gif.title;
        out.mediaName = rich.gif.title;
        out.mediaId = rich.gif.gifId;
        break;
    case MediaKind::Album: {
        out.body = rich.caption;
        QStringList ids;
        for (const MediaWire &wire : rich.items) {
            ids.append(wire.fileId);
            out.mediaSize += wire.size;
            out.mediaDurationMs += wire.durationMs;
            if (out.mediaName.isEmpty()) {
                out.mediaName = wire.name;
            }
        }
        out.mediaId = joinMediaIds(ids);
        break;
    }
    default: {
        out.body = rich.caption;
        if (!rich.items.isEmpty()) {
            const MediaWire &wire = rich.items.first();
            out.mediaId = wire.fileId;
            out.mediaName = wire.name;
            out.mediaSize = wire.size;
            out.mediaDurationMs = wire.durationMs;
        }
        break;
    }
    }
    return out;
}

// Builds the local media row from the descriptor inside the envelope. The
// upload id stays the same on both sides, so a chunk can be fetched directly.
std::optional<MediaRecord> mediaFromWire(storage::Database &db, const MediaWire &wire,
                                         const QString &chatId, MediaKind kind)
{
    MediaRecord record;
    record.mediaId = wire.fileId;
    record.chatId = chatId;
    record.kind = wire.kind;
    if (record.kind == MediaKind::File && kind != MediaKind::Text
        && kind != MediaKind::Album) {
        record.kind = kind;
    }
    if (record.kind == MediaKind::Text) {
        record.kind = MediaKind::File;
    }
    record.mime = wire.mime;
    record.name = wire.name;
    record.size = wire.size;
    record.chunkSize = wire.chunkSize;
    record.fileHash = wire.fileHash;
    record.fileKey = wire.fileKey;
    record.nonceBase = wire.nonceBase;
    record.width = wire.width;
    record.height = wire.height;
    record.durationMs = wire.durationMs;
    record.state = QStringLiteral("received");
    record.createdAt = Clock::nowMs();
    record.updatedAt = record.createdAt;
    storage::MediaRepo repo(db);
    if (repo.insert(record).isError()) {
        return std::nullopt;
    }
    if (!wire.thumb.isEmpty()) {
        repo.setMeta(record.mediaId, QStringLiteral("thumb_b64"),
                     QString::fromLatin1(wire.thumb.toBase64()));
    }
    return record;
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
    // §39.3: the cleanup pass runs every 30 s on the storage thread.
    m_expiryTimer.setInterval(30 * 1000);
    connect(&m_expiryTimer, &QTimer::timeout, this, &MessageService::runExpiryCleanup);
    if (m_storage) {
        m_expiryTimer.start();
    }
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
    Message message;
    message.chatId = chatId;
    message.body = text;
    message.kind = MediaKind::Text;
    message.replyToClientMsgId = replyToClientMsgId;
    message.clientMsgId = clientMsgId;
    return enqueueMessage(message);
}

QString MessageService::recordPlainText(const QString &chatId, const QString &text,
                                        const QString &serverMsgId)
{
    if (!m_storage) {
        emit errorOccurred(QStringLiteral("storage_unavailable"), QString());
        return {};
    }
    if (chatId.isEmpty() || text.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("empty_message"), QString());
        return {};
    }
    Message message;
    message.clientMsgId = Ids::clientMsgId();
    message.chatId = chatId;
    message.body = text;
    message.kind = MediaKind::Text;
    message.state = MessageState::Sent;
    message.outgoing = true;
    message.localTs = Clock::nowMs();
    message.serverTs = message.localTs;
    message.serverMsgId = serverMsgId;
    message.senderLogin = m_selfLogin;
    const QString id = message.clientMsgId;
    auto future = m_storage->submit([message](storage::Database &db) {
        auto inserted = storage::MessagesRepo(db).insertQueued(message);
        if (inserted.isError()) {
            return false;
        }
        auto updated = storage::MessagesRepo(db).updateState(
            message.clientMsgId, MessageState::Sent, message.localTs, message.serverMsgId);
        if (updated.isError()) {
            return false;
        }
        storage::ChatsRepo(db).touchLastMessage(message.chatId, message.localTs,
                                                message.body);
        return true;
    });
    watch<bool>(this, future, [this, message](bool stored) {
        if (!stored) {
            emit errorOccurred(QStringLiteral("message_store_failed"), QString());
            return;
        }
        emit messageAdded(message);
    });
    return id;
}

QString MessageService::sendMedia(const QString &chatId, const QString &mediaId,
                                  const QString &caption,
                                  const QString &replyToClientMsgId,
                                  const QString &clientMsgId)
{
    if (chatId.isEmpty() || mediaId.isEmpty()) {
        emit errorOccurred(QStringLiteral("invalid_media"), QString());
        return {};
    }
    Message message;
    message.chatId = chatId;
    message.mediaId = mediaId;
    message.body = caption.left(1024); // §11.5 #97
    message.replyToClientMsgId = replyToClientMsgId;
    message.clientMsgId = clientMsgId;
    return enqueueMessage(message);
}

QString MessageService::sendSticker(const QString &chatId, const QString &packId,
                                    const QString &stickerId, const QString &title,
                                    const QString &replyToClientMsgId,
                                    const QString &clientMsgId)
{
    if (chatId.isEmpty() || packId.isEmpty() || stickerId.isEmpty()
        || packId.contains(QLatin1Char('/')) || stickerId.contains(QLatin1Char('/'))
        || packId.contains(QLatin1Char(',')) || stickerId.contains(QLatin1Char(','))) {
        emit errorOccurred(QStringLiteral("invalid_sticker"), QString());
        return {};
    }
    Message message;
    message.chatId = chatId;
    message.kind = MediaKind::Sticker;
    message.mediaId = packId + QLatin1Char('/') + stickerId;
    message.body = title;
    message.mediaName = title;
    message.replyToClientMsgId = replyToClientMsgId;
    message.clientMsgId = clientMsgId;
    return enqueueMessage(message);
}

QString MessageService::sendGif(const QString &chatId, const QString &gifId,
                                const QString &title,
                                const QString &replyToClientMsgId,
                                const QString &clientMsgId)
{
    if (chatId.isEmpty() || gifId.isEmpty() || gifId.contains(QLatin1Char(','))) {
        emit errorOccurred(QStringLiteral("invalid_gif"), QString());
        return {};
    }
    Message message;
    message.chatId = chatId;
    message.kind = MediaKind::Gif;
    message.mediaId = gifId;
    message.body = title;
    message.mediaName = title;
    message.replyToClientMsgId = replyToClientMsgId;
    message.clientMsgId = clientMsgId;
    return enqueueMessage(message);
}

QString MessageService::sendAlbum(const QString &chatId, const QStringList &mediaIds,
                                  const QString &caption,
                                  const QString &replyToClientMsgId,
                                  const QString &clientMsgId)
{
    QStringList unique;
    for (const QString &mediaId : mediaIds) {
        const QString trimmed = mediaId.trimmed();
        if (trimmed.isEmpty() || trimmed.contains(QLatin1Char(','))) {
            emit errorOccurred(QStringLiteral("invalid_album"), QString());
            return {};
        }
        if (!unique.contains(trimmed)) {
            unique.append(trimmed);
        }
    }
    if (chatId.isEmpty() || unique.isEmpty() || unique.size() > 10) {
        emit errorOccurred(QStringLiteral("invalid_album"), QString());
        return {};
    }
    Message message;
    message.chatId = chatId;
    message.kind = MediaKind::Album;
    message.mediaId = joinMediaIds(unique);
    message.body = caption.left(1024); // §11.5 #97
    message.replyToClientMsgId = replyToClientMsgId;
    message.clientMsgId = clientMsgId;
    return enqueueMessage(message);
}

QString MessageService::sendPoll(const QString &chatId, const PollBody &poll,
                                 const QString &clientMsgId)
{
    const QString question = poll.question.trimmed();
    if (!m_storage || chatId.isEmpty() || question.isEmpty()
        || question.size() > kPollQuestionMax
        || poll.options.size() < kPollMinOptions || poll.options.size() > kPollMaxOptions) {
        emit errorOccurred(QStringLiteral("poll.invalid"), QString());
        return {};
    }
    QStringList options;
    for (const QString &raw : poll.options) {
        const QString option = raw.trimmed();
        if (option.isEmpty() || option.size() > kPollOptionMax) {
            emit errorOccurred(QStringLiteral("poll.invalid"), QString());
            return {};
        }
        options.append(option);
    }
    if (poll.quiz && (poll.correctOption < 0 || poll.correctOption >= options.size())) {
        emit errorOccurred(QStringLiteral("poll.invalid"), QString());
        return {};
    }

    const QString pollId =
        poll.pollId.isEmpty() ? Ids::clientMsgId() : poll.pollId;
    const QString messageId =
        clientMsgId.isEmpty() ? Ids::clientMsgId() : clientMsgId;

    Poll record;
    record.pollId = pollId;
    record.chatId = chatId;
    record.messageClientMsgId = messageId;
    record.question = question;
    record.options = options;
    record.anonymous = poll.anonymous;
    record.multiple = poll.multiple;
    record.quiz = poll.quiz;
    record.allowChange = poll.allowChange;
    record.correctOption = poll.quiz ? poll.correctOption : -1;
    record.createdBy = m_selfLogin;
    record.createdAt = Clock::nowMs();

    // The poll row must exist before the send pump reads the message body.
    m_storage->submit([record](storage::Database &db) {
        return storage::PollsRepo(db).upsert(record).isOk();
    });

    Message message;
    message.chatId = chatId;
    message.kind = MediaKind::Text;
    message.body = question;
    message.pollId = pollId;
    message.clientMsgId = messageId;
    return enqueueMessage(message);
}

QString MessageService::enqueueMessage(const Message &draft)
{
    if (!m_storage) {
        emit errorOccurred(QStringLiteral("storage_unavailable"), QString());
        return {};
    }

    Message message = draft;
    if (message.clientMsgId.isEmpty()) {
        message.clientMsgId = Ids::clientMsgId();
    }
    message.outgoing = true;
    message.state = MessageState::Queued;
    message.localTs = Clock::nowMs();
    if (message.kind == MediaKind::Text && message.mediaId.isEmpty()) {
        message.kind = MediaKind::Text;
    } else if (message.kind == MediaKind::Text) {
        // The caller knows only the id; the row carries the real kind.
        message.kind = MediaKind::File;
    } else if ((message.kind == MediaKind::Sticker || message.kind == MediaKind::Gif)
               && message.mediaId.isEmpty()) {
        emit errorOccurred(QStringLiteral("invalid_asset"), QString());
        return {};
    } else if (message.kind == MediaKind::Album && message.mediaId.isEmpty()) {
        emit errorOccurred(QStringLiteral("invalid_album"), QString());
        return {};
    }

    m_holdUntil.insert(message.clientMsgId, message.localTs + m_cancelWindowMs);

    const QString id = message.clientMsgId;
    auto future = m_storage->submit([message](storage::Database &db) {
        MediaKind kind = message.kind;
        if (kind == MediaKind::Album) {
            const QStringList ids = splitMediaIds(message.mediaId);
            if (ids.isEmpty() || ids.size() > 10) {
                return -2;
            }
            qint64 total = 0;
            qint64 totalDuration = 0;
            QString firstName;
            for (const QString &mediaId : ids) {
                const auto media = storage::MediaRepo(db).get(mediaId);
                if (media.isError() || !media.value().has_value()
                    || !mediaKindHasFile(media.value()->kind)) {
                    return -2;
                }
                total += media.value()->size;
                totalDuration += media.value()->durationMs;
                if (firstName.isEmpty()) {
                    firstName = media.value()->name;
                }
            }
            Message stored = message;
            stored.kind = kind;
            stored.mediaName = firstName;
            stored.mediaSize = total;
            stored.mediaDurationMs = totalDuration;
            auto inserted = storage::MessagesRepo(db).insertQueued(stored);
            if (inserted.isError()) {
                return -1;
            }
            if (!inserted.value()) {
                return 0;
            }
            auto enqueued = storage::OutboxRepo(db).enqueue(stored.chatId,
                                                            stored.clientMsgId,
                                                            stored.localTs);
            if (enqueued.isError()) {
                return -1;
            }
            auto touched = storage::ChatsRepo(db).touchLastMessage(
                stored.chatId, stored.localTs,
                mediaPreviewText(stored.kind, QString(), stored.body), false);
            return touched.isError() ? -1 : 1;
        }
        if (kind == MediaKind::Sticker || kind == MediaKind::Gif) {
            if (message.mediaId.isEmpty()) {
                return -2;
            }
            Message stored = message;
            auto inserted = storage::MessagesRepo(db).insertQueued(stored);
            if (inserted.isError()) {
                return -1;
            }
            if (!inserted.value()) {
                return 0;
            }
            auto enqueued = storage::OutboxRepo(db).enqueue(stored.chatId,
                                                            stored.clientMsgId,
                                                            stored.localTs);
            if (enqueued.isError()) {
                return -1;
            }
            auto touched = storage::ChatsRepo(db).touchLastMessage(
                stored.chatId, stored.localTs,
                mediaPreviewText(stored.kind, QString(), stored.body), false);
            return touched.isError() ? -1 : 1;
        }
        if (!message.mediaId.isEmpty()) {
            const auto media = storage::MediaRepo(db).get(message.mediaId);
            if (media.isError() || !media.value().has_value()) {
                return -2; // upload row is gone; do not send a broken reference
            }
            kind = media.value()->kind;
        } else if (kind != MediaKind::Text) {
            return -2;
        }
        Message stored = message;
        stored.kind = kind;
        if (!message.mediaId.isEmpty()) {
            const auto loaded = storage::MediaRepo(db).get(message.mediaId);
            if (loaded.isOk() && loaded.value().has_value()) {
                stored.mediaName = loaded.value()->name;
                stored.mediaSize = loaded.value()->size;
                stored.mediaDurationMs = loaded.value()->durationMs;
            }
        }
        auto inserted = storage::MessagesRepo(db).insertQueued(stored);
        if (inserted.isError()) {
            return -1;
        }
        if (!inserted.value()) {
            return 0; // duplicate client_msg_id: nothing new to send
        }
        auto enqueued = storage::OutboxRepo(db).enqueue(stored.chatId, stored.clientMsgId,
                                                        stored.localTs);
        if (enqueued.isError()) {
            return -1;
        }
        auto touched = storage::ChatsRepo(db).touchLastMessage(
            stored.chatId, stored.localTs,
            mediaPreviewText(stored.kind, QString(), stored.body), false);
        if (touched.isError()) {
            return -1;
        }
        return 1;
    });
    watch<int>(this, future, [this, message](int stored) {
        if (stored == -2) {
            m_holdUntil.remove(message.clientMsgId);
            emit errorOccurred(QStringLiteral("media_missing"), QString());
            return;
        }
        if (stored < 0) {
            m_holdUntil.remove(message.clientMsgId);
            emit errorOccurred(QStringLiteral("storage_failed"), QString());
            return;
        }
        if (stored == 0) {
            m_holdUntil.remove(message.clientMsgId);
            return;
        }
        if (message.mediaId.isEmpty()) {
            emit messageAdded(message);
            schedulePump(m_cancelWindowMs > 0 ? m_cancelWindowMs : 0);
            return;
        }
        // The caller does not know the media kind; the stored row does.
        const QString id = message.clientMsgId;
        auto loaded = m_storage->submit(
            [id](storage::Database &db) { return loadMessage(db, id); });
        watch<StoredMessage>(this, loaded, [this](StoredMessage current) {
            if (current.found) {
                emit messageAdded(current.message);
            }
            schedulePump(m_cancelWindowMs > 0 ? m_cancelWindowMs : 0);
        });
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

void MessageService::deleteForEveryone(const QString &clientMsgId, bool asAdmin, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    const qint64 now = Clock::nowMs();
    auto future = m_storage->submit([clientMsgId, asAdmin, now](storage::Database &db) {
        const StoredMessage stored = loadMessage(db, clientMsgId);
        if (!stored.found) {
            return QPair<int, QString>(0, QStringLiteral("not_found"));
        }
        if (!asAdmin) {
            if (!stored.message.outgoing) {
                return QPair<int, QString>(0, QStringLiteral("not_author"));
            }
            const qint64 base =
                stored.message.serverTs > 0 ? stored.message.serverTs : stored.message.localTs;
            if (now - base > kActionWindowMs) {
                return QPair<int, QString>(0, QStringLiteral("delete_window_expired"));
            }
        }
        auto removed = storage::MessagesRepo(db).deleteLocal(clientMsgId);
        if (removed.isError() || !removed.value()) {
            return QPair<int, QString>(-1, QStringLiteral("storage_failed"));
        }
        storage::ChatsRepo(db).recalculateLastMessage(stored.message.chatId);
        return QPair<int, QString>(1, stored.message.chatId);
    });
    watch<QPair<int, QString>>(
        this, future,
        [this, clientMsgId, done = std::move(done)](QPair<int, QString> result) {
            if (result.first == 1) {
                m_holdUntil.remove(clientMsgId);
                emit messageRemoved(clientMsgId, result.second);
                emit pinsChanged(result.second);
                QJsonObject payload;
                payload.insert(QStringLiteral("target"), clientMsgId);
                sendChatEvent(result.second, QStringLiteral("message_delete"), payload);
                done(Result<bool>::ok(true));
                return;
            }
            done(Result<bool>::error(result.second.isEmpty()
                                         ? QStringLiteral("delete_failed")
                                         : result.second));
        });
}

void MessageService::editMessage(const QString &clientMsgId, const QString &newBody,
                                 Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    if (newBody.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("empty_message")));
        return;
    }
    const qint64 now = Clock::nowMs();
    auto future = m_storage->submit([clientMsgId, newBody, now](storage::Database &db) {
        const StoredMessage stored = loadMessage(db, clientMsgId);
        if (!stored.found) {
            return QPair<int, QString>(0, QStringLiteral("not_found"));
        }
        if (!stored.message.outgoing) {
            return QPair<int, QString>(0, QStringLiteral("not_author"));
        }
        if (stored.message.kind != MediaKind::Text) {
            return QPair<int, QString>(0, QStringLiteral("not_text"));
        }
        if (stored.message.state == MessageState::DeletedLocal) {
            return QPair<int, QString>(0, QStringLiteral("deleted"));
        }
        const qint64 base =
            stored.message.serverTs > 0 ? stored.message.serverTs : stored.message.localTs;
        if (now - base > kActionWindowMs) {
            return QPair<int, QString>(0, QStringLiteral("edit_window_expired"));
        }
        auto updated = storage::MessagesRepo(db).updateBody(clientMsgId, newBody, now);
        if (updated.isError() || !updated.value()) {
            return QPair<int, QString>(-1, QStringLiteral("storage_failed"));
        }
        return QPair<int, QString>(1, stored.message.chatId);
    });
    watch<QPair<int, QString>>(
        this, future,
        [this, clientMsgId, newBody, done = std::move(done)](QPair<int, QString> result) {
            if (result.first != 1) {
                done(Result<bool>::error(result.second.isEmpty()
                                             ? QStringLiteral("edit_failed")
                                             : result.second));
                return;
            }
            const QString id = clientMsgId;
            auto loaded = m_storage->submit(
                [id](storage::Database &db) { return loadMessage(db, id); });
            watch<StoredMessage>(this, loaded, [this, newBody](StoredMessage current) {
                if (current.found) {
                    emit messageUpdated(current.message);
                }
            });
            QJsonObject payload;
            payload.insert(QStringLiteral("target"), clientMsgId);
            payload.insert(QStringLiteral("body"), newBody);
            sendChatEvent(result.second, QStringLiteral("message_edit"), payload);
            done(Result<bool>::ok(true));
        });
}

void MessageService::forwardMessages(const QStringList &sourceIds,
                                     const QStringList &targetChatIds, Done<int> done)
{
    if (!m_storage) {
        done(Result<int>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    if (sourceIds.isEmpty() || targetChatIds.isEmpty()) {
        done(Result<int>::ok(0));
        return;
    }
    auto future = m_storage->submit([sourceIds](storage::Database &db) {
        QVector<Message> sources;
        for (const QString &id : sourceIds) {
            const StoredMessage stored = loadMessage(db, id);
            if (stored.found) {
                sources.append(stored.message);
            }
        }
        return sources;
    });
    watch<QVector<Message>>(this, future,
                            [this, targetChatIds, done = std::move(done)](
                                QVector<Message> sources) {
        int queued = 0;
        for (const Message &source : sources) {
            for (const QString &target : targetChatIds) {
                if (target.isEmpty() || target == source.chatId) {
                    continue;
                }
                Message copy = source;
                copy.chatId = target;
                copy.clientMsgId = Ids::clientMsgId();
                copy.serverMsgId.clear();
                copy.serverTs = 0;
                copy.localTs = 0;
                copy.state = MessageState::Queued;
                copy.outgoing = true;
                copy.editedAt = 0;
                copy.replyToClientMsgId.clear();
                copy.pinnedAt = 0;
                copy.forwardedFrom = source.outgoing
                    ? (m_selfLogin.isEmpty() ? QStringLiteral("me") : m_selfLogin)
                    : source.senderLogin;
                if (!enqueueMessage(copy).isEmpty()) {
                    ++queued;
                }
            }
        }
        done(Result<int>::ok(queued));
    });
}

void MessageService::pinMessage(const QString &chatId, const QString &clientMsgId,
                                Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, clientMsgId](storage::Database &db) {
        storage::MessagesRepo repo(db);
        auto count = repo.pinCount(chatId);
        if (count.isError()) {
            return QPair<int, QString>(-1, QStringLiteral("storage_failed"));
        }
        if (count.value() >= storage::MessagesRepo::kMaxPins) {
            return QPair<int, QString>(0, QStringLiteral("pin_limit"));
        }
        const StoredMessage stored = loadMessage(db, clientMsgId);
        if (!stored.found) {
            return QPair<int, QString>(0, QStringLiteral("not_found"));
        }
        auto pinned = repo.setPinned(chatId, clientMsgId, Clock::nowMs(), QString());
        if (pinned.isError()) {
            return QPair<int, QString>(-1, QStringLiteral("storage_failed"));
        }
        return QPair<int, QString>(1, QString());
    });
    watch<QPair<int, QString>>(
        this, future,
        [this, chatId, done = std::move(done)](QPair<int, QString> result) {
            if (result.first == 1) {
                emit pinsChanged(chatId);
                done(Result<bool>::ok(true));
                return;
            }
            done(Result<bool>::error(result.second.isEmpty()
                                         ? QStringLiteral("pin_failed")
                                         : result.second));
        });
}

void MessageService::unpinMessage(const QString &chatId, const QString &clientMsgId,
                                  Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, clientMsgId](storage::Database &db) {
        return storage::MessagesRepo(db).unpin(chatId, clientMsgId);
    });
    watch<Result<bool>>(
        this, future, [this, chatId, done = std::move(done)](Result<bool> result) {
            if (result.isOk()) {
                emit pinsChanged(chatId);
            }
            done(std::move(result));
        });
}

void MessageService::listPinned(const QString &chatId, Done<QVector<Message>> done)
{
    if (!m_storage) {
        done(Result<QVector<Message>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        return storage::MessagesRepo(db).pinned(chatId);
    });
    watch<Result<QVector<Message>>>(
        this, future, [done = std::move(done)](Result<QVector<Message>> result) {
            done(std::move(result));
        });
}

void MessageService::searchInChat(const QString &chatId, const QString &query,
                                  Done<QVector<Message>> done)
{
    if (!m_storage) {
        done(Result<QVector<Message>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId, query](storage::Database &db) {
        return storage::MessagesRepo(db).searchInChat(chatId, query);
    });
    watch<Result<QVector<Message>>>(
        this, future, [done = std::move(done)](Result<QVector<Message>> result) {
            done(std::move(result));
        });
}

void MessageService::toggleReaction(const QString &clientMsgId, const QString &emoji,
                                    Done<bool> done)
{
    if (!m_storage || clientMsgId.isEmpty() || emoji.isEmpty() || m_selfLogin.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("reaction.invalid")));
        return;
    }
    const QString self = m_selfLogin;
    auto future = m_storage->submit([clientMsgId, emoji, self](storage::Database &db) {
        const auto lookup = storage::MessagesRepo(db).find(clientMsgId);
        if (lookup.isError() || !lookup.value().found) {
            return QPair<int, QString>{-1, QStringLiteral("reaction.message_missing")};
        }
        const Message &message = lookup.value().message;
        storage::ReactionsRepo repo(db);
        auto mine = repo.listForMessage(clientMsgId);
        if (mine.isError()) {
            return QPair<int, QString>{-1, mine.error()};
        }
        bool remove = false;
        for (const Reaction &reaction : mine.value()) {
            if (reaction.senderLogin.compare(self, Qt::CaseInsensitive) == 0) {
                remove = reaction.emoji == emoji;
                break;
            }
        }
        if (remove) {
            repo.remove(clientMsgId, self);
        } else {
            // §38.1: groups cap distinct reactions at 20.
            auto distinct = repo.distinctCount(clientMsgId);
            if (distinct.isOk() && distinct.value() >= kMaxDistinctReactions) {
                bool known = false;
                auto all = repo.listForMessage(clientMsgId);
                if (all.isOk()) {
                    for (const Reaction &reaction : all.value()) {
                        if (reaction.emoji == emoji) {
                            known = true;
                            break;
                        }
                    }
                }
                if (!known) {
                    return QPair<int, QString>{-1,
                                               QStringLiteral("reaction.limit")};
                }
            }
            Reaction reaction;
            reaction.clientMsgId = clientMsgId;
            reaction.chatId = message.chatId;
            reaction.senderLogin = self;
            reaction.emoji = emoji;
            reaction.createdAt = Clock::nowMs();
            repo.set(reaction);
        }
        return QPair<int, QString>{0, message.chatId};
    });
    watch<QPair<int, QString>>(
        this, future,
        [this, clientMsgId, emoji, done](QPair<int, QString> result) {
            if (result.first != 0) {
                done(Result<bool>::error(result.second));
                return;
            }
            const QString chatId = result.second;
            emit reactionsChanged(chatId, clientMsgId);
            QJsonObject payload;
            payload.insert(QStringLiteral("target"), clientMsgId);
            payload.insert(QStringLiteral("emoji"), emoji);
            sendChatEvent(chatId, QStringLiteral("reaction"), payload);
            done(Result<bool>::ok(true));
        });
}

void MessageService::reactionsForChat(const QString &chatId,
                                      Done<QVector<Reaction>> done)
{
    if (!m_storage) {
        done(Result<QVector<Reaction>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([chatId](storage::Database &db) {
        return storage::ReactionsRepo(db).listForChat(chatId);
    });
    watch<Result<QVector<Reaction>>>(
        this, future, [done = std::move(done)](Result<QVector<Reaction>> result) {
            done(std::move(result));
        });
}

void MessageService::sendControlEvent(const QString &chatId, const QString &kind,
                                      const QJsonObject &payload, Done<bool> done)
{
    if (!m_transport || chatId.isEmpty() || kind.isEmpty() || !m_crypto) {
        if (done) {
            done(Result<bool>::error(QStringLiteral("control_unavailable")));
        }
        return;
    }
    sendChatEvent(chatId, kind, payload);
    if (done) {
        done(Result<bool>::ok(true));
    }
}

void MessageService::setChatExpiry(const QString &chatId, qint64 afterMs, Done<bool> done)
{
    if (!m_storage || chatId.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    if (!disappearing::isValid(afterMs)) {
        done(Result<bool>::error(QStringLiteral("expiry.invalid")));
        return;
    }
    const QString self = m_selfLogin;
    const QString body =
        QCoreApplication::translate("MessageService", "Disappearing messages: %1")
            .arg(disappearing::label(afterMs));
    const qint64 now = Clock::nowMs();
    Message system;
    system.clientMsgId = Ids::clientMsgId();
    system.chatId = chatId;
    system.senderLogin = self;
    system.body = body;
    system.system = true;
    system.outgoing = false;
    system.state = MessageState::Received;
    system.localTs = now;
    system.serverTs = now;
    auto future = m_storage->submit([chatId, afterMs, system](storage::Database &db) {
        auto changed = storage::ChatsRepo(db).setExpiresAfter(chatId, afterMs);
        if (changed.isError()) {
            return changed;
        }
        // §39.2: changing the timer leaves a service line in the history.
        if (auto inserted = storage::MessagesRepo(db).insertReceived(system);
            inserted.isError()) {
            return inserted;
        }
        storage::ChatsRepo(db).touchLastMessage(chatId, system.serverTs, system.body,
                                                 false);
        return changed;
    });
    watch<Result<bool>>(this, future,
                        [this, chatId, afterMs, system, done](Result<bool> result) {
                            if (result.isError()) {
                                done(result);
                                return;
                            }
                            emit chatTimerChanged(chatId, afterMs);
                            emit messageAdded(system);
                            QJsonObject payload;
                            payload.insert(QStringLiteral("after_ms"),
                                           static_cast<double>(afterMs));
                            sendChatEvent(chatId, QStringLiteral("chat_timer"), payload);
                            done(Result<bool>::ok(true));
                        });
}

void MessageService::runExpiryCleanup()
{
    if (!m_storage) {
        return;
    }
    const qint64 now = Clock::nowMs();
    auto future = m_storage->submit([now](storage::Database &db) {
        auto removed = storage::MessagesRepo(db).removeExpired(now);
        if (removed.isOk()) {
            QStringList chats;
            for (const Message &message : removed.value()) {
                if (!chats.contains(message.chatId)) {
                    chats.append(message.chatId);
                }
            }
            for (const QString &chatId : std::as_const(chats)) {
                storage::ChatsRepo(db).recalculateLastMessage(chatId);
            }
        }
        return removed;
    });
    watch<Result<QVector<Message>>>(
        this, future, [this](Result<QVector<Message>> result) {
            if (result.isError()) {
                return;
            }
            QHash<QString, QStringList> targets;
            QStringList mediaIds;
            for (const Message &message : result.value()) {
                emit messageRemoved(message.clientMsgId, message.chatId);
                targets[message.chatId].append(message.clientMsgId);
                if (!message.mediaId.isEmpty()) {
                    mediaIds.append(message.mediaId);
                }
            }
            if (!mediaIds.isEmpty()) {
                emit mediaExpired(mediaIds);
            }
            for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
                emit pinsChanged(it.key());
                QJsonObject payload;
                QJsonArray ids;
                for (const QString &id : it.value()) {
                    ids.append(id);
                }
                payload.insert(QStringLiteral("targets"), ids);
                sendChatEvent(it.key(), QStringLiteral("expire"), payload);
            }
        });
}

void MessageService::setExpiryIntervalMs(int milliseconds)
{
    if (milliseconds <= 0) {
        m_expiryTimer.stop();
        return;
    }
    m_expiryTimer.setInterval(qMax(1000, milliseconds));
    m_expiryTimer.start();
}

void MessageService::sendChatEvent(const QString &chatId, const QString &kind,
                                   const QJsonObject &payload)
{
    if (!m_transport || chatId.isEmpty() || kind.isEmpty()) {
        return;
    }
    const QByteArray bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const bool channelChat = channel_ids::isChannelChat(chatId);
    const bool groupChat = group_ids::isGroupChat(chatId) || channelChat;

    if (groupChat) {
        GroupCryptoSession *session = channelChat ? m_channelCrypto : m_groupCrypto;
        if (!session) {
            return;
        }
        const QString containerId = channelChat
            ? channel_ids::channelIdFromChat(chatId)
            : group_ids::groupIdFromChat(chatId);
        auto dispatch = [this, chatId, kind, containerId, bytes]() {
            GroupCryptoSession *groupLike =
                channel_ids::isChannelChat(chatId) ? m_channelCrypto : m_groupCrypto;
            QJsonObject header;
            header.insert(QStringLiteral("ts"), static_cast<double>(Clock::nowMs()));
            auto sealed = groupLike->encryptGroup(containerId, bytes, &header);
            if (sealed.isError()) {
                return;
            }
            const bool channel = channel_ids::isChannelChat(chatId);
            auto future = m_storage->submit([containerId, channel](storage::Database &db) {
                QStringList logins;
                if (channel) {
                    auto members = storage::ChannelsRepo(db).members(containerId);
                    if (members.isOk()) {
                        for (const ChannelMember &member : members.value()) {
                            if (!member.login.isEmpty()) {
                                logins.append(member.login);
                            }
                        }
                    }
                } else {
                    auto members = storage::GroupsRepo(db).members(containerId);
                    if (members.isOk()) {
                        for (const GroupMember &member : members.value()) {
                            if (!member.login.isEmpty()) {
                                logins.append(member.login);
                            }
                        }
                    }
                }
                return logins;
            });
            watch<QStringList>(this, future,
                               [this, chatId, kind, header, ciphertext = sealed.value()](
                                   QStringList logins) {
                QList<net::Envelope> envelopes;
                for (const QString &login : std::as_const(logins)) {
                    if (login.compare(m_selfLogin, Qt::CaseInsensitive) == 0) {
                        continue;
                    }
                    QJsonObject copy = header;
                    copy.insert(QStringLiteral("to"), login);
                    envelopes.append(net::Envelope{kind, Ids::clientMsgId(), chatId,
                                                   ciphertext, copy});
                }
                if (!envelopes.isEmpty()) {
                    m_transport->send(envelopes, nullptr);
                }
            });
        };
        if (session->isGroupReady(containerId)) {
            dispatch();
        } else {
            session->prepareGroup(containerId, [dispatch](Result<bool> ready) {
                if (ready.isOk()) {
                    dispatch();
                }
            });
        }
        return;
    }

    if (!m_crypto) {
        return;
    }
    auto future =
        m_storage->submit([chatId](storage::Database &db) {
            return storage::ChatsRepo(db).peerLogin(chatId);
        });
    watch<Result<QString>>(
        this, future,
        [this, chatId, kind, bytes](Result<QString> peer) {
            const QString login = peer.isOk() ? peer.value() : QString();
            if (login.isEmpty() || !m_crypto) {
                return;
            }
            auto send = [this, chatId, kind, bytes, login](Result<bool> ready) {
                if (ready.isError() || !m_crypto || !m_transport) {
                    return;
                }
                QJsonObject header;
                header.insert(QStringLiteral("ts"), static_cast<double>(Clock::nowMs()));
                header.insert(QStringLiteral("to"), login);
                auto sealed = m_crypto->encrypt(chatId, bytes, &header);
                if (sealed.isError()) {
                    return;
                }
                m_transport->send({net::Envelope{kind, Ids::clientMsgId(), chatId,
                                                 sealed.value(), header}},
                                  nullptr);
            };
            if (m_crypto->isReady(chatId)) {
                send(Result<bool>::ok(true));
            } else {
                m_crypto->prepare(chatId, login, send);
            }
        });
}

void MessageService::applyIncomingEvent(const QString &kind, const QString &chatId,
                                        const QByteArray &plain, const QString &sender)
{
    if (!m_storage) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(plain);
    if (!document.isObject()) {
        return;
    }
    const QJsonObject payload = document.object();
    if (kind == QLatin1String("reaction")) {
        applyReactionEvent(chatId, payload, sender);
        return;
    }
    if (kind == QLatin1String("expire")) {
        applyExpireEvent(chatId, payload);
        return;
    }
    if (kind == QLatin1String("chat_timer")) {
        applyChatTimerEvent(chatId, payload);
        return;
    }
    const QString target = payload.value(QStringLiteral("target")).toString();
    if (target.isEmpty()) {
        return;
    }
    if (kind == QLatin1String("message_edit")) {
        const QString body = payload.value(QStringLiteral("body")).toString();
        if (body.trimmed().isEmpty()) {
            return;
        }
        auto future = m_storage->submit([target, body, sender](storage::Database &db) {
            const StoredMessage stored = loadMessage(db, target);
            if (!stored.found || stored.message.outgoing
                || stored.message.senderLogin.compare(sender, Qt::CaseInsensitive) != 0) {
                return StoredMessage{};
            }
            storage::MessagesRepo(db).updateBody(target, body, Clock::nowMs());
            return loadMessage(db, target);
        });
        watch<StoredMessage>(this, future, [this](StoredMessage updated) {
            if (updated.found) {
                emit messageUpdated(updated.message);
            }
        });
        return;
    }
    if (kind == QLatin1String("message_delete")) {
        auto future = m_storage->submit([target](storage::Database &db) {
            const StoredMessage stored = loadMessage(db, target);
            if (!stored.found) {
                return QString();
            }
            storage::MessagesRepo(db).deleteLocal(target);
            storage::ChatsRepo(db).recalculateLastMessage(stored.message.chatId);
            return stored.message.chatId;
        });
        watch<QString>(this, future, [this, target, chatId](QString removedChat) {
            if (!removedChat.isEmpty()) {
                emit messageRemoved(target, removedChat.isEmpty() ? chatId : removedChat);
                emit pinsChanged(removedChat);
            }
        });
    }
}

void MessageService::applyReactionEvent(const QString &chatId, const QJsonObject &payload,
                                        const QString &sender)
{
    const QString target = payload.value(QStringLiteral("target")).toString();
    const QString emoji = payload.value(QStringLiteral("emoji")).toString();
    if (target.isEmpty() || emoji.isEmpty() || sender.isEmpty()) {
        return;
    }
    auto future = m_storage->submit([chatId, target, emoji, sender](
                                        storage::Database &db) {
        const StoredMessage stored = loadMessage(db, target);
        if (!stored.found || stored.message.chatId != chatId) {
            return false; // a control event may not touch another chat
        }
        storage::ReactionsRepo repo(db);
        auto existing = repo.listForMessage(target);
        if (existing.isError()) {
            return false;
        }
        bool remove = false;
        for (const Reaction &reaction : existing.value()) {
            if (reaction.senderLogin.compare(sender, Qt::CaseInsensitive) == 0) {
                remove = reaction.emoji == emoji;
                break;
            }
        }
        if (remove) {
            repo.remove(target, sender);
        } else {
            auto distinct = repo.distinctCount(target);
            if (distinct.isOk() && distinct.value() >= kMaxDistinctReactions) {
                bool known = false;
                for (const Reaction &reaction : existing.value()) {
                    if (reaction.emoji == emoji) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    return false;
                }
            }
            Reaction reaction;
            reaction.clientMsgId = target;
            reaction.chatId = chatId;
            reaction.senderLogin = sender;
            reaction.emoji = emoji;
            reaction.createdAt = Clock::nowMs();
            repo.set(reaction);
        }
        return true;
    });
    watch<bool>(this, future, [this, chatId, target](bool changed) {
        if (changed) {
            emit reactionsChanged(chatId, target);
        }
    });
}

void MessageService::applyExpireEvent(const QString &chatId, const QJsonObject &payload)
{
    QStringList targets;
    const QJsonArray array = payload.value(QStringLiteral("targets")).toArray();
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            targets.append(value.toString());
        }
    }
    const QString single = payload.value(QStringLiteral("target")).toString();
    if (!single.isEmpty()) {
        targets.append(single);
    }
    if (targets.isEmpty()) {
        return;
    }
    auto future = m_storage->submit([chatId, targets](storage::Database &db) {
        QStringList removed;
        for (const QString &target : targets) {
            const StoredMessage stored = loadMessage(db, target);
            if (!stored.found || stored.message.chatId != chatId) {
                continue;
            }
            storage::MessagesRepo(db).deleteLocal(target);
            removed.append(target);
        }
        if (!removed.isEmpty()) {
            storage::ChatsRepo(db).recalculateLastMessage(chatId);
        }
        return removed;
    });
    watch<QStringList>(this, future, [this, chatId](QStringList removed) {
        for (const QString &target : std::as_const(removed)) {
            emit messageRemoved(target, chatId);
        }
        if (!removed.isEmpty()) {
            emit pinsChanged(chatId);
        }
    });
}

void MessageService::applyChatTimerEvent(const QString &chatId, const QJsonObject &payload)
{
    const qint64 afterMs =
        static_cast<qint64>(payload.value(QStringLiteral("after_ms")).toDouble(0));
    if (!disappearing::isValid(afterMs)) {
        return;
    }
    const QString self = m_selfLogin;
    const QString body =
        QCoreApplication::translate("MessageService", "Disappearing messages: %1")
            .arg(disappearing::label(afterMs));
    const qint64 now = Clock::nowMs();
    Message system;
    system.clientMsgId = Ids::clientMsgId();
    system.chatId = chatId;
    system.senderLogin = self;
    system.body = body;
    system.system = true;
    system.outgoing = false;
    system.state = MessageState::Received;
    system.localTs = now;
    system.serverTs = now;
    auto future = m_storage->submit([chatId, afterMs, system](storage::Database &db) {
        auto changed = storage::ChatsRepo(db).setExpiresAfter(chatId, afterMs);
        if (changed.isError()) {
            return changed;
        }
        if (auto inserted = storage::MessagesRepo(db).insertReceived(system);
            inserted.isError()) {
            return inserted;
        }
        storage::ChatsRepo(db).touchLastMessage(chatId, system.serverTs, system.body,
                                                 false);
        return changed;
    });
    watch<Result<bool>>(this, future,
                        [this, chatId, afterMs, system](Result<bool> result) {
                            if (result.isOk()) {
                                emit chatTimerChanged(chatId, afterMs);
                                emit messageAdded(system);
                            }
                        });
}

void MessageService::notifyPollChange(const QString &pollId)
{
    if (!m_storage || pollId.isEmpty()) {
        return;
    }
    auto future = m_storage->submit([pollId](storage::Database &db) {
        return storage::PollsRepo(db).get(pollId);
    });
    watch<Result<std::optional<Poll>>>(
        this, future, [this](Result<std::optional<Poll>> result) {
            if (result.isOk() && result.value().has_value()) {
                emit pollsChanged(result.value()->chatId, result.value()->pollId);
            }
        });
}

void MessageService::loadHistory(const QString &chatId, const MessageCursor &before, int limit,
                                 Done<QVector<Message>> done){
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
        // §39.2: reading starts the disappearing timer, so the chat row is
        // read first and its value passed into the same transaction.
        auto chat = storage::ChatsRepo(db).get(chatId);
        const qint64 after = chat.isOk() && chat.value().has_value()
            ? chat.value()->expiresAfterMs
            : 0;
        return storage::MessagesRepo(db).markRead(chatId, after, Clock::nowMs());
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
        const bool channelChat = channel_ids::isChannelChat(item.chatId);
        const bool groupChat = group_ids::isGroupChat(item.chatId) || channelChat;
        GroupCryptoSession *groupLike =
            channelChat ? m_channelCrypto : m_groupCrypto;
        if (!stored.bodyOk || !stored.wireOk || (!groupChat && !m_crypto)) {
            m_sendingChats.remove(item.chatId);
            markFailed(item, stored.wireOk ? QStringLiteral("crypto_unavailable")
                                           : QStringLiteral("media_missing"));
            return;
        }
        if (groupChat && !groupLike) {
            m_sendingChats.remove(item.chatId);
            markFailed(item, channelChat ? QStringLiteral("channel_crypto_unavailable")
                                         : QStringLiteral("group_crypto_unavailable"));
            return;
        }

        auto dispatch = [this, item, stored](const QList<net::Envelope> &envelopes) {
            Message sending = stored.message;
            sending.state = MessageState::Sending;
            const QString id = item.clientMsgId;
            auto marked = m_storage->submit([id](storage::Database &db) {
                return storage::MessagesRepo(db)
                    .updateState(id, MessageState::Sending)
                    .isOk();
            });
            watch<bool>(this, marked, [this, item, sending, envelopes](bool) {
                emit messageUpdated(sending);
                m_transport->send(
                    envelopes,
                    [this, item](const net::ApiResult<net::SendResult> &result) {
                        m_sendingChats.remove(item.chatId);
                        const bool accepted = result.isOk()
                            && result.value().accepted.contains(item.clientMsgId);
                        if (accepted) {
                            const QString id = item.clientMsgId;
                            const qint64 outboxId = item.id;
                            auto stored = m_storage->submit(
                                [id, outboxId](storage::Database &db) {
                                    storage::MessagesRepo(db).updateState(id,
                                                                          MessageState::Sent);
                                    storage::OutboxRepo(db).remove(outboxId);
                                    return loadMessage(db, id);
                                });
                            watch<StoredMessage>(this, stored, [this](StoredMessage updated) {
                                if (updated.found) {
                                    emit messageUpdated(updated.message);
                                }
                                schedulePump(0);
                            });
                            return;
                        }
                        if (result.isOk()) {
                            markFailed(item, QStringLiteral("send_rejected"));
                            return;
                        }
                        if (result.error().isRetryable()) {
                            handleSendFailure(item, result.error());
                            return;
                        }
                        markFailed(item, result.error().code.isEmpty()
                                            ? QStringLiteral("send_failed")
                                            : result.error().code);
                    });
            });
        };

        if (groupChat) {
            const QString containerId = channelChat
                ? channel_ids::channelIdFromChat(item.chatId)
                : group_ids::groupIdFromChat(item.chatId);
            if (!groupLike->isGroupReady(containerId)) {
                const QString chatId = item.chatId;
                groupLike->prepareGroup(containerId, [this, item, chatId](Result<bool> ready) {
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
            header.insert(QStringLiteral("ts"), static_cast<double>(Clock::nowMs()));
            auto sealed = groupLike->encryptGroup(containerId, stored.wire, &header);
            if (sealed.isError()) {
                m_sendingChats.remove(item.chatId);
                markFailed(item, sealed.error());
                return;
            }
            if (!stored.message.replyToClientMsgId.isEmpty()) {
                header.insert(QStringLiteral("reply_to"),
                              stored.message.replyToClientMsgId);
            }
            if (!stored.message.forwardedFrom.isEmpty()) {
                header.insert(QStringLiteral("forwarded_from"),
                              stored.message.forwardedFrom);
            }
            // The server routes mail by header.to, so a broadcast is one
            // envelope per other member (§43.3). The roster comes from the
            // local cache that GroupService/ChannelService keeps in sync.
            const QString chatId = item.chatId;
            const QByteArray ciphertext = sealed.value();
            const QString kind = channelChat ? QStringLiteral("channel_text")
                                             : QStringLiteral("group_text");
            auto future = m_storage->submit([chatId, channelChat](storage::Database &db) {
                QStringList logins;
                if (channelChat) {
                    auto members = storage::ChannelsRepo(db).members(
                        channel_ids::channelIdFromChat(chatId));
                    if (members.isOk()) {
                        for (const ChannelMember &member : members.value()) {
                            if (!member.login.isEmpty()) {
                                logins.append(member.login);
                            }
                        }
                    }
                } else {
                    auto members = storage::GroupsRepo(db).members(
                        group_ids::groupIdFromChat(chatId));
                    if (members.isOk()) {
                        for (const GroupMember &member : members.value()) {
                            if (!member.login.isEmpty()) {
                                logins.append(member.login);
                            }
                        }
                    }
                }
                return logins;
            });
            watch<QStringList>(
                this, future,
                [this, item, chatId, header, ciphertext, kind, dispatch](QStringList logins) {
                    QList<net::Envelope> envelopes;
                    for (const QString &login : std::as_const(logins)) {
                        if (login.compare(m_selfLogin, Qt::CaseInsensitive) == 0) {
                            continue;
                        }
                        QJsonObject copy = header;
                        copy.insert(QStringLiteral("to"), login);
                        envelopes.append(net::Envelope{kind, item.clientMsgId, chatId,
                                                       ciphertext, copy});
                    }
                    if (envelopes.isEmpty()) {
                        // Nobody else is subscribed: the post is delivered to
                        // the local history but there is no recipient to mail.
                        m_sendingChats.remove(item.chatId);
                        const QString id = item.clientMsgId;
                        const qint64 outboxId = item.id;
                        auto storedRow = m_storage->submit([id, outboxId](storage::Database &db) {
                            storage::MessagesRepo(db).updateState(id, MessageState::Sent);
                            storage::OutboxRepo(db).remove(outboxId);
                            return loadMessage(db, id);
                        });
                        watch<StoredMessage>(this, storedRow, [this](StoredMessage updated) {
                            if (updated.found) {
                                emit messageUpdated(updated.message);
                            }
                            schedulePump(0);
                        });
                        return;
                    }
                    dispatch(envelopes);
                });
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
        auto sealed = m_crypto->encrypt(item.chatId, stored.wire, &header);
        if (sealed.isError()) {
            m_sendingChats.remove(item.chatId);
            markFailed(item, QStringLiteral("crypto_failed"));
            return;
        }
        if (!stored.message.replyToClientMsgId.isEmpty()) {
            header.insert(QStringLiteral("reply_to"), stored.message.replyToClientMsgId);
        }
        if (!stored.message.forwardedFrom.isEmpty()) {
            header.insert(QStringLiteral("forwarded_from"), stored.message.forwardedFrom);
        }
        dispatch({net::Envelope{QStringLiteral("text"), item.clientMsgId, item.chatId,
                                sealed.value(), header}});
    });
}

void MessageService::sendControlMessage(const QString &peerLogin, const QString &kind,
                                        const QByteArray &payload, Done<bool> done)
{
    if (!m_crypto || !m_transport || m_selfLogin.isEmpty() || peerLogin.isEmpty()
        || kind.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("control_unavailable")));
        return;
    }
    const QString chatId = ChatService::directChatId(m_selfLogin, peerLogin);
    auto send = [this, chatId, kind, payload, done, peerLogin](Result<bool> ready) {
        if (ready.isError()) {
            done(Result<bool>::error(ready.error()));
            return;
        }
        QJsonObject header;
        header.insert(QStringLiteral("ts"), static_cast<double>(Clock::nowMs()));
        header.insert(QStringLiteral("control"), kind);
        auto sealed = m_crypto->encrypt(chatId, payload, &header);
        if (sealed.isError()) {
            done(Result<bool>::error(sealed.error()));
            return;
        }
        // The server routes every envelope by header.to (§30.5).
        header.insert(QStringLiteral("to"), peerLogin);
        const net::Envelope envelope{kind, Ids::clientMsgId(), chatId, sealed.value(),
                                     header};
        m_transport->send({envelope}, [done](const net::ApiResult<net::SendResult> &result) {
            if (result.isError()) {
                done(Result<bool>::error(result.error().code.isEmpty()
                                             ? QStringLiteral("control_send_failed")
                                             : result.error().code));
                return;
            }
            done(Result<bool>::ok(true));
        });
    };
    if (m_crypto->isReady(chatId)) {
        send(Result<bool>::ok(true));
        return;
    }
    m_crypto->prepare(chatId, peerLogin, std::move(send));
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

void MessageService::handleBotIncoming(const net::Envelope &envelope, const QString &sender)
{
    if (!m_storage || m_selfLogin.isEmpty()) {
        return;
    }
    const QString chatId = ChatService::directChatId(m_selfLogin, sender);
    const QString body = QString::fromUtf8(envelope.ciphertext);
    const QString displayName =
        envelope.header.value(QStringLiteral("bot_display_name")).toString();

    Message message;
    message.clientMsgId = envelope.clientMsgId;
    message.chatId = chatId;
    message.body = body;
    message.kind = MediaKind::Text;
    message.senderLogin = sender;
    message.outgoing = false;
    message.state = MessageState::Received;
    message.localTs = Clock::nowMs();
    const qint64 serverTs =
        static_cast<qint64>(envelope.header.value(QStringLiteral("ts")).toDouble(0));
    message.serverTs = serverTs;

    auto future = m_storage->submit([message, sender, displayName](storage::Database &db) {
        storage::ChatsRepo chats(db);
        auto existing = chats.get(message.chatId);
        if (existing.isError()) {
            return -1;
        }
        if (!existing.value().has_value()) {
            Chat chat;
            chat.chatId = message.chatId;
            chat.kind = QStringLiteral("direct");
            chat.peerLogin = sender;
            chat.title = displayName.isEmpty() ? sender : displayName;
            chat.updatedAt =
                message.serverTs > 0 ? message.serverTs : message.localTs;
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
        if (stored > 0) {
            emit messageAdded(message);
        }
    });
}

void MessageService::handleIncoming(const net::Envelope &envelope)
{
    if (!m_storage) {
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
    if (envelope.kind == QStringLiteral("group_text")
        || envelope.kind == QStringLiteral("channel_text")
        || group_ids::isGroupChat(envelope.chatId)
        || channel_ids::isChannelChat(envelope.chatId)) {
        handleGroupIncoming(envelope, sender, false);
        return;
    }
    // §29.14: a bot reply is plaintext, is labelled `bot` and is addressed to
    // the account id rather than the direct chat id.
    if (envelope.kind == QStringLiteral("bot")
        || envelope.header.value(QStringLiteral("bot")).toBool()) {
        handleBotIncoming(envelope, sender);
        return;
    }
    if (!m_crypto) {
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

    if (envelope.kind == QStringLiteral("sender_key_distribution")) {
        // Pairwise control traffic: hand the plaintext payload to GroupService
        // and never show it in the history.
        emit controlMessageReceived(sender, envelope.kind, plain.value());
        return;
    }
    if (envelope.kind == QStringLiteral("reaction")
        || envelope.kind == QStringLiteral("expire")
        || envelope.kind == QStringLiteral("chat_timer")) {
        applyIncomingEvent(envelope.kind, envelope.chatId, plain.value(), sender);
        return;
    }
    if (envelope.kind == QStringLiteral("poll_vote")
        || envelope.kind == QStringLiteral("poll_close")) {
        // §38.3: PollService owns the tally; MessageService only decrypts.
        emit controlMessageReceived(sender, envelope.kind, plain.value());
        return;
    }
    if (envelope.kind == QStringLiteral("message_edit")
        || envelope.kind == QStringLiteral("message_delete")) {
        applyIncomingEvent(envelope.kind, envelope.chatId, plain.value(), sender);
        return;
    }

    const IncomingRich rich = makeIncomingRich(plain.value());

    Message message;
    message.clientMsgId = envelope.clientMsgId;
    message.chatId = envelope.chatId;
    message.body = rich.body;
    message.kind = rich.kind;
    message.mediaId = rich.mediaId;
    message.mediaName = rich.mediaName;
    message.mediaSize = rich.mediaSize;
    message.mediaDurationMs = rich.mediaDurationMs;
    message.pollId = rich.hasPoll ? rich.poll.pollId : QString();
    message.senderLogin = sender;
    message.replyToClientMsgId =
        envelope.header.value(QStringLiteral("reply_to")).toString();
    message.forwardedFrom =
        envelope.header.value(QStringLiteral("forwarded_from")).toString();
    message.outgoing = false;
    message.state = MessageState::Received;
    message.localTs = Clock::nowMs();
    const qint64 serverTs =
        static_cast<qint64>(envelope.header.value(QStringLiteral("ts")).toDouble(0));
    message.serverTs = serverTs;

    const QVector<MediaWire> wires = rich.wires;
    const bool hasPoll = rich.hasPoll;
    const PollBody pollDef = rich.poll;
    auto future = m_storage->submit([message, wires, hasPoll, pollDef](storage::Database &db) {
        for (const MediaWire &wire : wires) {
            if (!mediaFromWire(db, wire, message.chatId, message.kind).has_value()) {
                return -1;
            }
        }
        if (hasPoll) {
            Poll poll;
            poll.pollId = pollDef.pollId;
            poll.chatId = message.chatId;
            poll.messageClientMsgId = message.clientMsgId;
            poll.question = pollDef.question;
            poll.options = pollDef.options;
            poll.anonymous = pollDef.anonymous;
            poll.multiple = pollDef.multiple;
            poll.quiz = pollDef.quiz;
            poll.allowChange = pollDef.allowChange;
            poll.correctOption = pollDef.correctOption;
            poll.createdBy = message.senderLogin;
            poll.createdAt = message.serverTs > 0 ? message.serverTs : message.localTs;
            if (storage::PollsRepo(db).upsert(poll).isError()) {
                return -1;
            }
        }
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
            mediaPreviewText(message.kind, QString(), message.body), true);
        return touched.isError() ? -1 : 1;
    });
    watch<int>(this, future, [this, message](int stored) {
        if (stored < 0) {
            emit errorOccurred(QStringLiteral("storage_failed"), QString());
            return;
        }
        if (stored == 1) {
            emit messageAdded(message);
            if (!message.pollId.isEmpty()) {
                emit pollsChanged(message.chatId, message.pollId);
            }
        }
    });
}

void MessageService::handleGroupIncoming(const net::Envelope &envelope,
                                         const QString &senderLogin, bool keyPrepared)
{
    const bool channelChat = channel_ids::isChannelChat(envelope.chatId);
    QString groupId = envelope.header.value(QStringLiteral("group_id")).toString();
    if (groupId.isEmpty()) {
        groupId = channelChat ? channel_ids::channelIdFromChat(envelope.chatId)
                              : group_ids::groupIdFromChat(envelope.chatId);
    }
    const QString expectedChat =
        channelChat ? channel_ids::chatId(groupId) : group_ids::chatId(groupId);
    if (groupId.isEmpty() || expectedChat != envelope.chatId) {
        emit errorOccurred(QStringLiteral("chat_id_mismatch"), QString());
        return;
    }
    GroupCryptoSession *session = channelChat ? m_channelCrypto : m_groupCrypto;
    if (!session) {
        emit errorOccurred(channelChat ? QStringLiteral("channel_crypto_unavailable")
                                       : QStringLiteral("group_crypto_unavailable"),
                           QString());
        return;
    }
    if (!keyPrepared) {
        session->prepareSenderKey(
            groupId, senderLogin, [this, envelope, senderLogin](Result<bool> ready) {
                if (ready.isError()) {
                    emit errorOccurred(QStringLiteral("sender_key_missing"), ready.error());
                    return;
                }
                handleGroupIncoming(envelope, senderLogin, true);
            });
        return;
    }

    auto plain = session->decryptGroup(groupId, senderLogin, envelope.ciphertext,
                                       envelope.header);
    if (plain.isError()) {
        emit errorOccurred(QStringLiteral("decrypt_failed"), plain.error());
        return;
    }

    if (envelope.kind == QStringLiteral("message_edit")
        || envelope.kind == QStringLiteral("message_delete")
        || envelope.kind == QStringLiteral("reaction")
        || envelope.kind == QStringLiteral("expire")
        || envelope.kind == QStringLiteral("chat_timer")) {
        applyIncomingEvent(envelope.kind, envelope.chatId, plain.value(), senderLogin);
        return;
    }
    if (envelope.kind == QStringLiteral("poll_vote")
        || envelope.kind == QStringLiteral("poll_close")) {
        emit controlMessageReceived(senderLogin, envelope.kind, plain.value());
        return;
    }

    const IncomingRich rich = makeIncomingRich(plain.value());

    Message message;
    message.clientMsgId = envelope.clientMsgId;
    message.chatId = envelope.chatId;
    message.body = rich.body;
    message.kind = rich.kind;
    message.mediaId = rich.mediaId;
    message.mediaName = rich.mediaName;
    message.mediaSize = rich.mediaSize;
    message.mediaDurationMs = rich.mediaDurationMs;
    message.pollId = rich.hasPoll ? rich.poll.pollId : QString();
    message.senderLogin = senderLogin;
    message.replyToClientMsgId =
        envelope.header.value(QStringLiteral("reply_to")).toString();
    message.forwardedFrom =
        envelope.header.value(QStringLiteral("forwarded_from")).toString();
    message.outgoing = false;
    message.state = MessageState::Received;
    message.localTs = Clock::nowMs();
    const qint64 serverTs =
        static_cast<qint64>(envelope.header.value(QStringLiteral("ts")).toDouble(0));
    message.serverTs = serverTs;
    const QString groupIdCopy = groupId;
    const bool channelCopy = channelChat;
    const QVector<MediaWire> wires = rich.wires;
    const bool hasPoll = rich.hasPoll;
    const PollBody pollDef = rich.poll;

    auto future = m_storage->submit([message, groupIdCopy, channelCopy,
                                     wires, hasPoll, pollDef](storage::Database &db) {
        for (const MediaWire &wire : wires) {
            if (!mediaFromWire(db, wire, message.chatId, message.kind).has_value()) {
                return -1;
            }
        }
        if (hasPoll) {
            Poll poll;
            poll.pollId = pollDef.pollId;
            poll.chatId = message.chatId;
            poll.messageClientMsgId = message.clientMsgId;
            poll.question = pollDef.question;
            poll.options = pollDef.options;
            poll.anonymous = pollDef.anonymous;
            poll.multiple = pollDef.multiple;
            poll.quiz = pollDef.quiz;
            poll.allowChange = pollDef.allowChange;
            poll.correctOption = pollDef.correctOption;
            poll.createdBy = message.senderLogin;
            poll.createdAt = message.serverTs > 0 ? message.serverTs : message.localTs;
            if (storage::PollsRepo(db).upsert(poll).isError()) {
                return -1;
            }
        }
        storage::ChatsRepo chats(db);
        auto existing = chats.get(message.chatId);
        if (existing.isError()) {
            return -1;
        }
        if (!existing.value().has_value()) {
            Chat chat;
            chat.chatId = message.chatId;
            chat.kind = channelCopy ? QStringLiteral("channel") : QStringLiteral("group");
            chat.title = groupIdCopy;
            if (channelCopy) {
                auto channel = storage::ChannelsRepo(db).getChannel(groupIdCopy);
                if (channel.isOk() && channel.value().has_value()
                    && !channel.value().value().title.isEmpty()) {
                    chat.title = channel.value().value().title;
                }
            } else {
                auto group = storage::GroupsRepo(db).getGroup(groupIdCopy);
                if (group.isOk() && group.value().has_value()
                    && !group.value().value().title.isEmpty()) {
                    chat.title = group.value().value().title;
                }
            }
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
            mediaPreviewText(message.kind, QString(), message.body), true);
        return touched.isError() ? -1 : 1;
    });
    watch<int>(this, future, [this, message](int stored) {
        if (stored < 0) {
            emit errorOccurred(QStringLiteral("storage_failed"), QString());
            return;
        }
        if (stored == 1) {
            emit messageAdded(message);
            if (!message.pollId.isEmpty()) {
                emit pollsChanged(message.chatId, message.pollId);
            }
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
    if (!m_storage) {
        return;
    }
    auto future = m_storage->submit([chatId, clientMsgIds](storage::Database &db) {
        QVector<Message> changed;
        for (const QString &clientMsgId : clientMsgIds) {
            Advance advance = advanceMessage(db, clientMsgId, MessageState::Read, 0);
            if (advance.changed) {
                changed.append(advance.message);
            }
        }
        // §39.2: our outgoing messages start their countdown when the peer
        // confirms it has read them.
        auto chat = storage::ChatsRepo(db).get(chatId);
        if (chat.isOk() && chat.value().has_value()
            && chat.value()->expiresAfterMs > 0) {
            storage::MessagesRepo(db).applyReadExpiry(
                clientMsgIds, chat.value()->expiresAfterMs, Clock::nowMs());
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
