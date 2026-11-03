#include "services/ChannelService.h"

#include "core/Clock.h"
#include "domain/Chat.h"
#include "services/MessageService.h"
#include "storage/ChannelsRepo.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/StorageWorker.h"

#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <utility>

namespace mini::services {
namespace {

constexpr int kMaxTitle = 128;
constexpr int kMaxAbout = 2000;

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

QString peerKey(const QString &channelId, const QString &sender)
{
    return channelId + QLatin1Char('|') + sender;
}

} // namespace

ChannelService::ChannelService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void ChannelService::setMessageService(MessageService *messages)
{
    if (m_messages == messages) {
        return;
    }
    if (m_messages) {
        disconnect(m_messages, nullptr, this, nullptr);
    }
    m_messages = messages;
    if (!m_messages) {
        return;
    }
    connect(m_messages, &MessageService::controlMessageReceived, this,
            [this](const QString &senderLogin, const QString &kind, const QByteArray &payload) {
                if (kind != QStringLiteral("channel_sender_key")) {
                    return;
                }
                const QJsonObject object = QJsonDocument::fromJson(payload).object();
                const QString channelId =
                    object.value(QStringLiteral("group_id")).toString();
                if (channelId.isEmpty()) {
                    return;
                }
                handleSenderKeyDistribution(channelId, senderLogin, payload);
            });
}

void ChannelService::setSelfIdentity(const QString &userId, const QString &login)
{
    m_selfUserId = userId;
    m_selfLogin = login;
}

Result<ChannelDraft> ChannelService::validateDraft(const ChannelDraft &draft)
{
    ChannelDraft clean = draft;
    clean.title = clean.title.trimmed();
    clean.about = clean.about.trimmed();
    if (clean.title.isEmpty() || clean.title.size() > kMaxTitle) {
        return Result<ChannelDraft>::error(QStringLiteral("channel.title_invalid"));
    }
    if (clean.about.size() > kMaxAbout) {
        return Result<ChannelDraft>::error(QStringLiteral("channel.about_too_long"));
    }
    return Result<ChannelDraft>::ok(clean);
}

Channel ChannelService::channelFromInfo(const net::ChannelInfo &info)
{
    Channel channel;
    channel.channelId = info.channelId;
    channel.title = info.title;
    channel.about = info.about;
    channel.avatarMedia = info.avatarMedia;
    channel.ownerId = info.ownerId;
    channel.isPublic = info.isPublic;
    channel.linkedGroup = info.linkedGroup;
    channel.inviteToken = info.inviteToken;
    channel.inviteLink = info.inviteLink;
    channel.isMember = !info.myRole.isEmpty();
    channel.myRole = channelRoleFromString(info.myRole);
    channel.myPermissions = info.myPermissions;
    channel.subscriberCount = info.subscriberCount;
    channel.createdAt = info.createdAt;
    channel.updatedAt = info.updatedAt;
    for (const net::ChannelMemberInfo &member : info.members) {
        channel.members.append(memberFromInfo(member));
    }
    if (channel.subscriberCount == 0 && !channel.members.isEmpty()) {
        channel.subscriberCount = channel.members.size();
    }
    return channel;
}

ChannelMember ChannelService::memberFromInfo(const net::ChannelMemberInfo &info)
{
    ChannelMember member;
    member.userId = info.userId;
    member.login = info.login;
    member.displayName = info.displayName;
    member.role = channelRoleFromString(info.role);
    member.permissions = info.permissions;
    member.joinedAt = info.joinedAt;
    return member;
}

QString ChannelService::apiError(const net::ApiError &error)
{
    return error.code.isEmpty() ? QStringLiteral("api_error") : error.code;
}

void ChannelService::persistChannel(const net::ChannelInfo &info, bool withMembers,
                                    Done<Channel> done)
{
    Channel channel = channelFromInfo(info);
    if (channel.channelId.isEmpty() || !m_storage) {
        done(Result<Channel>::error(QStringLiteral("channel.invalid_response")));
        return;
    }
    auto future = m_storage->submit([channel, withMembers](storage::Database &db) {
        storage::ChannelsRepo repo(db);
        if (repo.upsertChannel(channel).isError()) {
            return false;
        }
        if (withMembers && !channel.members.isEmpty()) {
            return repo.replaceMembers(channel.channelId, channel.members).isOk();
        }
        return true;
    });
    watch<bool>(this, future, [this, channel, done = std::move(done)](bool saved) {
        if (!saved) {
            done(Result<Channel>::error(QStringLiteral("channel.persist_failed")));
            return;
        }
        emit channelUpdated(channel.channelId);
        emit channelsChanged();
        done(Result<Channel>::ok(channel));
    });
}

void ChannelService::refreshMembers(const QString &channelId,
                                    Done<QVector<ChannelMember>> done)
{
    if (!m_api) {
        done(Result<QVector<ChannelMember>>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->members(channelId, [this, channelId, done = std::move(done)](
                                  net::ApiResult<QList<net::ChannelMemberInfo>> result) {
        if (result.isError()) {
            done(Result<QVector<ChannelMember>>::error(apiError(result.error())));
            return;
        }
        QVector<ChannelMember> members;
        for (const net::ChannelMemberInfo &info : result.value()) {
            members.append(memberFromInfo(info));
        }
        if (!m_storage) {
            done(Result<QVector<ChannelMember>>::ok(members));
            return;
        }
        auto future = m_storage->submit([channelId, members](storage::Database &db) {
            storage::ChannelsRepo repo(db);
            if (!members.isEmpty()
                && repo.replaceMembers(channelId, members).isError()) {
                return false;
            }
            auto existing = repo.getChannel(channelId);
            if (existing.isError() || !existing.value().has_value()) {
                return true;
            }
            Channel channel = existing.value().value();
            channel.subscriberCount = members.size();
            return repo.upsertChannel(channel).isOk();
        });
        watch<bool>(this, future, [this, channelId, members, done](bool saved) {
            if (!saved) {
                done(Result<QVector<ChannelMember>>::error(
                    QStringLiteral("channel.persist_failed")));
                return;
            }
            emit channelUpdated(channelId);
            done(Result<QVector<ChannelMember>>::ok(members));
        });
    });
}

void ChannelService::ensureChat(const Channel &channel, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    Chat chat;
    chat.chatId = channel_ids::chatId(channel.channelId);
    chat.kind = QStringLiteral("channel");
    chat.title = channel.title.isEmpty() ? channel.channelId : channel.title;
    chat.updatedAt = channel.updatedAt > 0 ? channel.updatedAt : Clock::nowMs();
    auto future = m_storage->submit([chat](storage::Database &db) {
        return storage::ChatsRepo(db).upsert(chat).isOk();
    });
    watch<bool>(this, future, [done = std::move(done)](bool saved) {
        done(saved ? Result<bool>::ok(true)
                   : Result<bool>::error(QStringLiteral("chat.persist_failed")));
    });
}

void ChannelService::removeLocalChannel(const QString &channelId, Done<bool> done)
{
    m_localKeys.remove(channelId);
    for (auto it = m_peerKeys.begin(); it != m_peerKeys.end();) {
        if (it.key().startsWith(channelId + QLatin1Char('|'))) {
            it = m_peerKeys.erase(it);
        } else {
            ++it;
        }
    }
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([channelId](storage::Database &db) {
        const bool removed = storage::ChannelsRepo(db).removeChannel(channelId).isOk();
        storage::ChatsRepo(db).remove(channel_ids::chatId(channelId));
        return removed;
    });
    watch<bool>(this, future, [this, done = std::move(done)](bool removed) {
        emit channelsChanged();
        done(Result<bool>::ok(removed));
    });
}

void ChannelService::createChannel(const ChannelDraft &draft, Done<Channel> done)
{
    const Result<ChannelDraft> valid = validateDraft(draft);
    if (valid.isError()) {
        done(Result<Channel>::error(valid.error()));
        return;
    }
    if (!m_api) {
        done(Result<Channel>::error(QStringLiteral("api_unavailable")));
        return;
    }
    const ChannelDraft clean = valid.value();
    net::ChannelCreateRequest request;
    request.title = clean.title;
    request.about = clean.about;
    request.avatarMedia = clean.avatarMedia;
    request.isPublic = clean.isPublic;
    request.linkedGroup = clean.linkedGroup;

    m_api->createChannel(
        request, [this, done = std::move(done)](net::ApiResult<net::ChannelInfo> result) {
            if (result.isError()) {
                done(Result<Channel>::error(apiError(result.error())));
                return;
            }
            persistChannel(result.value(), true, [this, done](Result<Channel> saved) {
                if (saved.isError()) {
                    done(saved);
                    return;
                }
                const Channel channel = saved.value();
                ensureChat(channel, [this, channel, done](Result<bool>) {
                    prepareGroup(channel.channelId, [this, channel, done](Result<bool> ready) {
                        if (ready.isError()) {
                            done(Result<Channel>::error(ready.error()));
                            return;
                        }
                        distributeSenderKey(channel.channelId, [done, channel](Result<int>) {
                            done(Result<Channel>::ok(channel));
                        });
                    });
                });
            });
        });
}

void ChannelService::listChannels(Done<QVector<Channel>> done)
{
    if (!m_api) {
        done(Result<QVector<Channel>>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->listChannels([this, done = std::move(done)](
                            net::ApiResult<QList<net::ChannelInfo>> result) {
        if (result.isError()) {
            done(Result<QVector<Channel>>::error(apiError(result.error())));
            return;
        }
        QVector<Channel> channels;
        for (const net::ChannelInfo &info : result.value()) {
            channels.append(channelFromInfo(info));
        }
        if (!m_storage) {
            done(Result<QVector<Channel>>::ok(channels));
            return;
        }
        const QVector<Channel> toStore = channels;
        auto future = m_storage->submit([toStore](storage::Database &db) {
            storage::ChannelsRepo repo(db);
            for (const Channel &channel : toStore) {
                if (repo.upsertChannel(channel).isError()) {
                    return false;
                }
            }
            return true;
        });
        watch<bool>(this, future, [this, channels, done](bool stored) {
            if (!stored) {
                done(Result<QVector<Channel>>::error(
                    QStringLiteral("channel.persist_failed")));
                return;
            }
            emit channelsChanged();
            done(Result<QVector<Channel>>::ok(channels));
        });
    });
}

void ChannelService::loadChannel(const QString &channelId, Done<Channel> done)
{
    if (!m_api) {
        done(Result<Channel>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->getChannel(
        channelId, [this, channelId, done = std::move(done)](
                       net::ApiResult<net::ChannelInfo> result) {
            if (result.isError()) {
                done(Result<Channel>::error(apiError(result.error())));
                return;
            }
            const net::ChannelInfo info = result.value();
            const bool canSeeMembers = !info.myRole.isEmpty()
                && (info.myRole == QLatin1String("owner")
                    || info.myRole == QLatin1String("admin"));
            persistChannel(info, !info.members.isEmpty(), [this, channelId, canSeeMembers,
                                                           done](Result<Channel> saved) {
                if (saved.isError() || !canSeeMembers) {
                    done(saved);
                    return;
                }
                const Channel base = saved.value();
                refreshMembers(channelId, [this, channelId, base, done](
                                              Result<QVector<ChannelMember>> members) {
                    Channel channel = base;
                    if (members.isOk()) {
                        channel.members = members.value();
                        channel.subscriberCount = members.value().size();
                    }
                    // A publisher must make sure every subscriber holds the
                    // current chain; harmless when there are no new ones.
                    if (channel.canPublish()) {
                        distributeSenderKey(channelId, [](Result<int>) {});
                    }
                    done(Result<Channel>::ok(channel));
                });
            });
        });
}

void ChannelService::updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                                   Done<Channel> done)
{
    if (!m_api) {
        done(Result<Channel>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->updateChannel(
        channelId, patch, [this, done = std::move(done)](net::ApiResult<net::ChannelInfo> result) {
            if (result.isError()) {
                done(Result<Channel>::error(apiError(result.error())));
                return;
            }
            persistChannel(result.value(), false, std::move(done));
        });
}

void ChannelService::deleteChannel(const QString &channelId, Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->deleteChannel(channelId, [this, channelId, done = std::move(done)](
                                         net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        removeLocalChannel(channelId, std::move(done));
    });
}

void ChannelService::subscribe(const QString &channelId, const QString &token,
                               Done<Channel> done)
{
    if (!m_api) {
        done(Result<Channel>::error(QStringLiteral("api_unavailable")));
        return;
    }
    const QString trimmedToken = token.trimmed();
    m_api->subscribe(
        channelId, trimmedToken,
        [this, channelId, done = std::move(done)](net::ApiResult<QString> result) {
            if (result.isError()) {
                done(Result<Channel>::error(apiError(result.error())));
                return;
            }
            loadChannel(channelId, [this, channelId, done](Result<Channel> loaded) {
                if (loaded.isError()) {
                    done(loaded);
                    return;
                }
                const Channel channel = loaded.value();
                ensureChat(channel, [done, channel](Result<bool>) {
                    done(Result<Channel>::ok(channel));
                });
            });
        });
}

void ChannelService::unsubscribe(const QString &channelId, Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->unsubscribe(channelId, [this, channelId, done = std::move(done)](
                                       net::ApiResult<QString> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        removeLocalChannel(channelId, std::move(done));
    });
}

void ChannelService::members(const QString &channelId, Done<QVector<ChannelMember>> done)
{
    refreshMembers(channelId, std::move(done));
}

void ChannelService::setRole(const QString &channelId, const QString &userId,
                             ChannelRole role, quint32 permissions,
                             Done<ChannelMember> done)
{
    if (!m_api) {
        done(Result<ChannelMember>::error(QStringLiteral("api_unavailable")));
        return;
    }
    const quint32 mask = role == ChannelRole::Admin
        ? (permissions & channel_permissions::All)
        : 0;
    m_api->setRole(
        channelId, userId, channelRoleToString(role), mask,
        [this, channelId, role, done = std::move(done)](
            net::ApiResult<net::ChannelMemberInfo> result) {
            if (result.isError()) {
                done(Result<ChannelMember>::error(apiError(result.error())));
                return;
            }
            const ChannelMember member = memberFromInfo(result.value());
            if (!m_storage) {
                done(Result<ChannelMember>::ok(member));
                return;
            }
            auto future = m_storage->submit([channelId, member](storage::Database &db) {
                return storage::ChannelsRepo(db).upsertMember(channelId, member).isOk();
            });
            watch<bool>(this, future, [this, channelId, role, member, done](bool) {
                emit channelUpdated(channelId);
                if (role == ChannelRole::Admin) {
                    rotateSenderKey(channelId, [this, channelId, member, done](Result<bool>) {
                        distributeSenderKey(channelId, [member, done](Result<int>) {
                            done(Result<ChannelMember>::ok(member));
                        });
                    });
                    return;
                }
                done(Result<ChannelMember>::ok(member));
            });
        });
}

void ChannelService::createInvite(const QString &channelId, Done<QString> done)
{
    if (!m_api) {
        done(Result<QString>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->createInvite(
        channelId, [this, channelId, done = std::move(done)](
                       net::ApiResult<net::ChannelInvite> result) {
            if (result.isError()) {
                done(Result<QString>::error(apiError(result.error())));
                return;
            }
            net::ChannelInvite invite = result.value();
            if (invite.link.isEmpty() && !invite.token.isEmpty()) {
                invite.link = QStringLiteral("mini://c/") + invite.token;
            }
            if (!m_storage) {
                done(Result<QString>::ok(invite.link));
                return;
            }
            auto future = m_storage->submit([channelId, invite](storage::Database &db) {
                storage::ChannelsRepo repo(db);
                auto existing = repo.getChannel(channelId);
                if (existing.isError() || !existing.value().has_value()) {
                    return false;
                }
                Channel channel = existing.value().value();
                channel.inviteToken = invite.token;
                channel.inviteLink = invite.link;
                return repo.upsertChannel(channel).isOk();
            });
            watch<bool>(this, future, [this, channelId, link = invite.link, done](bool saved) {
                if (saved) {
                    emit channelUpdated(channelId);
                }
                done(Result<QString>::ok(link));
            });
        });
}

void ChannelService::recordView(const QString &channelId, const QString &postId,
                                Done<int> done)
{
    if (!m_api) {
        done(Result<int>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->recordView(
        channelId, postId, [done = std::move(done)](net::ApiResult<net::ChannelViewInfo> result) {
            if (result.isError()) {
                done(Result<int>::error(apiError(result.error())));
                return;
            }
            done(Result<int>::ok(result.value().views));
        });
}

void ChannelService::loadLocalSenderKey(
    const QString &channelId,
    std::function<void(Result<crypto::SenderKeyState>)> done)
{
    if (m_localKeys.contains(channelId)) {
        done(Result<crypto::SenderKeyState>::ok(m_localKeys.value(channelId)));
        return;
    }
    if (!m_storage) {
        done(Result<crypto::SenderKeyState>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    auto future = m_storage->submit([channelId, senderId](storage::Database &db) {
        return storage::ChannelsRepo(db).senderKey(channelId, senderId);
    });
    watch<Result<std::optional<crypto::SenderKeyState>>>(
        this, future,
        [this, channelId, done = std::move(done)](
            Result<std::optional<crypto::SenderKeyState>> result) {
            if (result.isError()) {
                done(Result<crypto::SenderKeyState>::error(result.error()));
                return;
            }
            if (!result.value().has_value()) {
                done(Result<crypto::SenderKeyState>::error(
                    QStringLiteral("sender_key_missing")));
                return;
            }
            m_localKeys.insert(channelId, result.value().value());
            done(Result<crypto::SenderKeyState>::ok(result.value().value()));
        });
}

void ChannelService::saveLocalSenderKey(const QString &channelId,
                                        const crypto::SenderKeyState &state,
                                        std::function<void(bool)> done)
{
    m_localKeys.insert(channelId, state);
    if (!m_storage) {
        if (done) {
            done(false);
        }
        return;
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    auto future = m_storage->submit([channelId, senderId, state](storage::Database &db) {
        return storage::ChannelsRepo(db).upsertSenderKey(channelId, senderId, state).isOk();
    });
    if (done) {
        watch<bool>(this, future, [done = std::move(done)](bool saved) { done(saved); });
    }
}

void ChannelService::prepareGroup(const QString &channelId,
                                  std::function<void(Result<bool>)> done)
{
    if (m_localKeys.contains(channelId)) {
        done(Result<bool>::ok(true));
        return;
    }
    loadLocalSenderKey(channelId, [this, channelId, done](Result<crypto::SenderKeyState> loaded) {
        if (loaded.isOk()) {
            done(Result<bool>::ok(true));
            return;
        }
        auto generated = crypto::SenderKey::generate(1);
        if (generated.isError()) {
            done(Result<bool>::error(generated.error()));
            return;
        }
        saveLocalSenderKey(channelId, generated.value());
        done(Result<bool>::ok(true));
    });
}

bool ChannelService::isGroupReady(const QString &channelId) const
{
    return m_localKeys.contains(channelId);
}

void ChannelService::resolveSenderId(const QString &channelId, const QString &senderLogin,
                                     std::function<void(QString)> done)
{
    if (!m_storage) {
        done(QString());
        return;
    }
    auto future = m_storage->submit([channelId, senderLogin](storage::Database &db) {
        auto member = storage::ChannelsRepo(db).memberByLogin(channelId, senderLogin);
        if (member.isOk() && member.value().has_value()
            && !member.value().value().userId.isEmpty()) {
            return member.value().value().userId;
        }
        return QString();
    });
    watch<QString>(this, future, [done = std::move(done)](QString senderId) {
        done(senderId);
    });
}

void ChannelService::prepareSenderKey(const QString &channelId, const QString &senderLogin,
                                      std::function<void(Result<bool>)> done)
{
    const QString key = peerKey(channelId, senderLogin);
    if (m_peerKeys.contains(key)) {
        done(Result<bool>::ok(true));
        return;
    }
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    resolveSenderId(channelId, senderLogin, [this, channelId, senderLogin, done](
                                                QString senderId) {
        auto future = m_storage->submit([channelId, senderId, senderLogin](storage::Database &db) {
            storage::ChannelsRepo repo(db);
            auto stored = repo.senderKey(channelId, senderId);
            if (stored.isError()) {
                return stored;
            }
            if (stored.value().has_value()) {
                return stored;
            }
            if (senderId != senderLogin) {
                return repo.senderKey(channelId, senderLogin);
            }
            return stored;
        });
        watch<Result<std::optional<crypto::SenderKeyState>>>(
            this, future,
            [this, channelId, senderLogin, done](
                Result<std::optional<crypto::SenderKeyState>> result) {
                if (result.isError()) {
                    done(Result<bool>::error(result.error()));
                    return;
                }
                if (!result.value().has_value()) {
                    done(Result<bool>::error(QStringLiteral("sender_key_missing")));
                    return;
                }
                m_peerKeys.insert(peerKey(channelId, senderLogin), result.value().value());
                done(Result<bool>::ok(true));
            });
    });
}

Result<QByteArray> ChannelService::encryptGroup(const QString &channelId,
                                                const QByteArray &plaintext,
                                                QJsonObject *header)
{
    auto it = m_localKeys.find(channelId);
    if (it == m_localKeys.end()) {
        return Result<QByteArray>::error(QStringLiteral("channel_key_not_ready"));
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    crypto::SenderKeyState &state = it.value();
    auto sealed = crypto::SenderKey::encrypt(state, channelId, senderId, plaintext, header);
    if (sealed.isError()) {
        return sealed;
    }
    const crypto::SenderKeyState snapshot = state;
    saveLocalSenderKey(channelId, snapshot);
    return sealed;
}

Result<QByteArray> ChannelService::decryptGroup(const QString &channelId,
                                                const QString &senderLogin,
                                                const QByteArray &ciphertext,
                                                const QJsonObject &header)
{
    auto parsed = crypto::SenderKeyHeader::fromJson(header);
    if (parsed.isError()) {
        return Result<QByteArray>::error(parsed.error());
    }
    if (parsed.value().groupId != channelId) {
        return Result<QByteArray>::error(QStringLiteral("channel_mismatch"));
    }
    const QString key = peerKey(channelId, senderLogin);
    auto it = m_peerKeys.find(key);
    if (it == m_peerKeys.end()) {
        return Result<QByteArray>::error(QStringLiteral("sender_key_missing"));
    }
    const QByteArray signature =
        QByteArray::fromBase64(header.value(QStringLiteral("sig")).toString().toLatin1());
    crypto::SenderKeyState &state = it.value();
    auto plain = crypto::SenderKey::decrypt(state, parsed.value(), ciphertext, signature);
    if (plain.isError()) {
        return plain;
    }
    const QString senderId = parsed.value().senderId;
    if (m_storage) {
        const crypto::SenderKeyState snapshot = state;
        auto future = m_storage->submit([channelId, senderId, snapshot](storage::Database &db) {
            return storage::ChannelsRepo(db).upsertSenderKey(channelId, senderId, snapshot)
                .isOk();
        });
        Q_UNUSED(future);
    }
    return plain;
}

void ChannelService::rotateSenderKey(const QString &channelId, Done<bool> done)
{
    loadLocalSenderKey(channelId, [this, channelId, done](Result<crypto::SenderKeyState> loaded) {
        quint32 nextId = 1;
        if (loaded.isOk()) {
            nextId = loaded.value().keyId + 1;
        }
        auto generated = crypto::SenderKey::generate(nextId);
        if (generated.isError()) {
            done(Result<bool>::error(generated.error()));
            return;
        }
        saveLocalSenderKey(channelId, generated.value());
        emit channelUpdated(channelId);
        done(Result<bool>::ok(true));
    });
}

void ChannelService::distributeSenderKey(const QString &channelId, Done<int> done)
{
    prepareGroup(channelId, [this, channelId, done](Result<bool> ready) {
        if (ready.isError()) {
            done(Result<int>::error(ready.error()));
            return;
        }
        if (!m_messages || !m_storage) {
            done(Result<int>::ok(0));
            return;
        }
        const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
        const QByteArray payload = crypto::SenderKey::serializeDistribution(
            channelId, senderId, m_localKeys.value(channelId));
        auto future = m_storage->submit([channelId](storage::Database &db) {
            return storage::ChannelsRepo(db).members(channelId);
        });
        watch<Result<QVector<ChannelMember>>>(
            this, future,
            [this, channelId, payload, done](Result<QVector<ChannelMember>> result) {
                if (result.isError()) {
                    done(Result<int>::error(result.error()));
                    return;
                }
                auto state = std::make_shared<int>(0);
                auto remaining = std::make_shared<int>(0);
                for (const ChannelMember &member : result.value()) {
                    if (member.login.isEmpty()
                        || member.login.compare(m_selfLogin, Qt::CaseInsensitive) == 0) {
                        continue;
                    }
                    ++(*remaining);
                    m_messages->sendControlMessage(
                        member.login, QStringLiteral("channel_sender_key"), payload,
                        [state, remaining, done](Result<bool> sent) {
                            if (sent.isOk()) {
                                ++(*state);
                            }
                            --(*remaining);
                            if (*remaining == 0) {
                                done(Result<int>::ok(*state));
                            }
                        });
                }
                if (*remaining == 0) {
                    done(Result<int>::ok(0));
                }
            });
    });
}

void ChannelService::handleSenderKeyDistribution(const QString &channelId,
                                                 const QString &senderLogin,
                                                 const QByteArray &payload)
{
    auto parsed = crypto::SenderKey::parseDistribution(payload, QString());
    if (parsed.isError()) {
        emit errorOccurred(QStringLiteral("sender_key_invalid"), parsed.error());
        return;
    }
    const crypto::SenderKeyState state = parsed.value();
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    if (object.value(QStringLiteral("group_id")).toString() != channelId) {
        emit errorOccurred(QStringLiteral("channel_mismatch"), channelId);
        return;
    }
    const QString payloadSenderId = object.value(QStringLiteral("sender_id")).toString();

    resolveSenderId(channelId, senderLogin,
                    [this, channelId, senderLogin, payloadSenderId, state](
                        QString memberId) {
        // A member may only publish their own chain. When the roster is not
        // cached yet the pairwise-authenticated login is the only identity we
        // have, so the payload id is accepted and bound to that login.
        if (!memberId.isEmpty() && !payloadSenderId.isEmpty()
            && payloadSenderId != memberId) {
            emit errorOccurred(QStringLiteral("sender_key_sender_mismatch"),
                               payloadSenderId);
            return;
        }
        const QString senderKey = peerKey(channelId, senderLogin);
        const auto existing = m_peerKeys.constFind(senderKey);
        if (existing != m_peerKeys.constEnd()) {
            if (existing.value().keyId > state.keyId
                || (existing.value().keyId == state.keyId
                    && existing.value().iteration > state.iteration)) {
                return; // an older distribution must not roll the chain back
            }
        }
        m_peerKeys.insert(senderKey, state);
        if (!m_storage) {
            emit senderKeyStored(channelId, senderLogin);
            return;
        }
        const QString storedId = payloadSenderId.isEmpty() ? senderLogin : payloadSenderId;
        auto future = m_storage->submit(
            [channelId, storedId, senderLogin, state](storage::Database &db) {
                storage::ChannelsRepo repo(db);
                bool stored = repo.upsertSenderKey(channelId, storedId, state).isOk();
                if (senderLogin != storedId) {
                    stored = repo.upsertSenderKey(channelId, senderLogin, state).isOk()
                        && stored;
                }
                return stored;
            });
        watch<bool>(this, future, [this, channelId, senderLogin](bool) {
            emit senderKeyStored(channelId, senderLogin);
            emit channelUpdated(channelId);
        });
    });
}

} // namespace mini::services
