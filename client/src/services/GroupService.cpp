#include "services/GroupService.h"

#include "core/Clock.h"
#include "domain/Chat.h"
#include "services/MessageService.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/GroupsRepo.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <memory>
#include <utility>

namespace mini::services {
namespace {

constexpr int kMaxTitle = 128;

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

QString peerKey(const QString &groupId, const QString &sender)
{
    return groupId + QLatin1Char('|') + sender;
}

} // namespace

GroupService::GroupService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void GroupService::setMessageService(MessageService *messages)
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
                if (kind != QStringLiteral("sender_key_distribution")) {
                    return;
                }
                const QJsonObject object =
                    QJsonDocument::fromJson(payload).object();
                const QString groupId = object.value(QStringLiteral("group_id")).toString();
                if (groupId.isEmpty()) {
                    return;
                }
                handleSenderKeyDistribution(groupId, senderLogin, payload);
            });
}

void GroupService::setSelfIdentity(const QString &userId, const QString &login)
{
    m_selfUserId = userId;
    m_selfLogin = login;
}

Result<GroupDraft> GroupService::validateDraft(const GroupDraft &draft)
{
    GroupDraft clean = draft;
    clean.title = clean.title.trimmed();
    clean.about = clean.about.trimmed();
    if (clean.title.isEmpty() || clean.title.size() > kMaxTitle) {
        return Result<GroupDraft>::error(QStringLiteral("group.title_invalid"));
    }
    QSet<QString> seen;
    QStringList members;
    for (const QString &raw : draft.members) {
        const QString login = raw.trimmed();
        if (login.isEmpty() || login.size() > 64) {
            continue;
        }
        const QString folded = login.toLower();
        if (seen.contains(folded)) {
            continue;
        }
        seen.insert(folded);
        members.append(login);
    }
    if (members.isEmpty()) {
        return Result<GroupDraft>::error(QStringLiteral("group.members_required"));
    }
    clean.members = members;
    return Result<GroupDraft>::ok(clean);
}

Group GroupService::groupFromInfo(const net::GroupInfo &info)
{
    Group group;
    group.groupId = info.groupId;
    group.title = info.title;
    group.about = info.about;
    group.avatarMedia = info.avatarMedia;
    group.ownerId = info.ownerId;
    group.isPublic = info.isPublic;
    group.slowModeSec = info.slowModeSec;
    group.joinByRequest = info.joinByRequest;
    group.inviteToken = info.inviteToken;
    group.inviteLink = info.inviteLink;
    group.myRole = groupRoleFromString(info.myRole);
    group.myPermissions = info.myPermissions;
    group.memberCount = info.memberCount;
    group.createdAt = info.createdAt;
    group.updatedAt = info.updatedAt;
    for (const net::GroupMemberInfo &member : info.members) {
        group.members.append(memberFromInfo(member));
    }
    if (group.memberCount == 0 && !group.members.isEmpty()) {
        group.memberCount = group.members.size();
    }
    return group;
}

GroupMember GroupService::memberFromInfo(const net::GroupMemberInfo &info)
{
    GroupMember member;
    member.userId = info.userId;
    member.login = info.login;
    member.displayName = info.displayName;
    member.role = groupRoleFromString(info.role);
    member.permissions = info.permissions;
    member.joinedAt = info.joinedAt;
    member.mutedUntil = info.mutedUntil;
    return member;
}

QString GroupService::apiError(const net::ApiError &error)
{
    return error.code.isEmpty() ? QStringLiteral("api_error") : error.code;
}

void GroupService::persistGroup(const net::GroupInfo &info, bool withMembers,
                                Done<Group> done)
{
    Group group = groupFromInfo(info);
    if (group.groupId.isEmpty() || !m_storage) {
        done(Result<Group>::error(QStringLiteral("group.invalid_response")));
        return;
    }
    auto future = m_storage->submit([group, withMembers](storage::Database &db) {
        storage::GroupsRepo repo(db);
        if (repo.upsertGroup(group).isError()) {
            return false;
        }
        if (withMembers && !group.members.isEmpty()) {
            return repo.replaceMembers(group.groupId, group.members).isOk();
        }
        return true;
    });
    watch<bool>(this, future, [this, group, done = std::move(done)](bool saved) {
        if (!saved) {
            done(Result<Group>::error(QStringLiteral("group.persist_failed")));
            return;
        }
        emit groupUpdated(group.groupId);
        emit groupsChanged();
        done(Result<Group>::ok(group));
    });
}

void GroupService::ensureChat(const Group &group, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    Chat chat;
    chat.chatId = group_ids::chatId(group.groupId);
    chat.kind = QStringLiteral("group");
    chat.title = group.title.isEmpty() ? group.groupId : group.title;
    chat.updatedAt = group.updatedAt > 0 ? group.updatedAt : Clock::nowMs();
    auto future = m_storage->submit([chat](storage::Database &db) {
        return storage::ChatsRepo(db).upsert(chat).isOk();
    });
    watch<bool>(this, future, [done = std::move(done)](bool saved) {
        done(saved ? Result<bool>::ok(true)
                   : Result<bool>::error(QStringLiteral("chat.persist_failed")));
    });
}

void GroupService::removeLocalGroup(const QString &groupId, Done<bool> done)
{
    m_localKeys.remove(groupId);
    for (auto it = m_peerKeys.begin(); it != m_peerKeys.end();) {
        if (it.key().startsWith(groupId + QLatin1Char('|'))) {
            it = m_peerKeys.erase(it);
        } else {
            ++it;
        }
    }
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([groupId](storage::Database &db) {
        const bool removed = storage::GroupsRepo(db).removeGroup(groupId).isOk();
        storage::ChatsRepo(db).remove(group_ids::chatId(groupId));
        return removed;
    });
    watch<bool>(this, future, [this, done = std::move(done)](bool removed) {
        emit groupsChanged();
        done(Result<bool>::ok(removed));
    });
}

void GroupService::createGroup(const GroupDraft &draft, Done<Group> done)
{
    const Result<GroupDraft> valid = validateDraft(draft);
    if (valid.isError()) {
        done(Result<Group>::error(valid.error()));
        return;
    }
    if (!m_api) {
        done(Result<Group>::error(QStringLiteral("api_unavailable")));
        return;
    }
    const GroupDraft clean = valid.value();
    net::GroupCreateRequest request;
    request.title = clean.title;
    request.about = clean.about;
    request.isPublic = clean.isPublic;
    request.joinByRequest = clean.joinByRequest;
    request.slowModeSec = clean.slowModeSec;
    request.members = clean.members;

    m_api->createGroup(request, [this, done = std::move(done)](net::ApiResult<net::GroupInfo> result) {
        if (result.isError()) {
            done(Result<Group>::error(apiError(result.error())));
            return;
        }
        persistGroup(result.value(), true, [this, done](Result<Group> saved) {
            if (saved.isError()) {
                done(saved);
                return;
            }
            const Group group = saved.value();
            ensureChat(group, [this, group, done](Result<bool>) {
                prepareGroup(group.groupId, [this, group, done](Result<bool> ready) {
                    if (ready.isError()) {
                        done(Result<Group>::error(ready.error()));
                        return;
                    }
                    distributeSenderKey(group.groupId, [done, group](Result<int>) {
                        done(Result<Group>::ok(group));
                    });
                });
            });
        });
    });
}

void GroupService::listGroups(Done<QVector<Group>> done)
{
    if (!m_api) {
        if (!m_storage) {
            done(Result<QVector<Group>>::error(QStringLiteral("storage_unavailable")));
            return;
        }
        auto future = m_storage->submit(
            [](storage::Database &db) { return storage::GroupsRepo(db).listGroups(); });
        watch<Result<QVector<Group>>>(this, future,
                                      [done = std::move(done)](Result<QVector<Group>> result) {
                                          done(std::move(result));
                                      });
        return;
    }
    m_api->listGroups([this, done = std::move(done)](
                          net::ApiResult<QList<net::GroupInfo>> result) {
        if (result.isError()) {
            done(Result<QVector<Group>>::error(apiError(result.error())));
            return;
        }
        QVector<Group> groups;
        for (const net::GroupInfo &info : result.value()) {
            groups.append(groupFromInfo(info));
        }
        if (!m_storage) {
            done(Result<QVector<Group>>::ok(groups));
            return;
        }
        const QVector<Group> toStore = groups;
        auto future = m_storage->submit([toStore](storage::Database &db) {
            storage::GroupsRepo repo(db);
            for (const Group &group : toStore) {
                if (repo.upsertGroup(group).isError()) {
                    return false;
                }
            }
            return true;
        });
        watch<bool>(this, future, [this, groups, done](bool stored) {
            if (!stored) {
                done(Result<QVector<Group>>::error(QStringLiteral("group.persist_failed")));
                return;
            }
            emit groupsChanged();
            done(Result<QVector<Group>>::ok(groups));
        });
    });
}

void GroupService::loadGroup(const QString &groupId, Done<Group> done)
{
    if (!m_api) {
        done(Result<Group>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->getGroup(groupId, [this, done = std::move(done)](net::ApiResult<net::GroupInfo> result) {
        if (result.isError()) {
            done(Result<Group>::error(apiError(result.error())));
            return;
        }
        persistGroup(result.value(), true, std::move(done));
    });
}

void GroupService::addMember(const QString &groupId, const QString &login,
                             Done<GroupMember> done)
{
    if (!m_api) {
        done(Result<GroupMember>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->addMember(groupId, login,
                     [this, groupId, done = std::move(done)](
                         net::ApiResult<net::GroupMemberInfo> result) {
        if (result.isError()) {
            done(Result<GroupMember>::error(apiError(result.error())));
            return;
        }
        const GroupMember member = memberFromInfo(result.value());
        m_api->getGroup(groupId, [this, groupId, member, done](
                                     net::ApiResult<net::GroupInfo> refreshed) {
            if (refreshed.isError()) {
                done(Result<GroupMember>::ok(member));
                return;
            }
            persistGroup(refreshed.value(), true, [this, groupId, member, done](
                                                    Result<Group> saved) {
                Q_UNUSED(saved);
                distributeSenderKey(groupId, [member, done](Result<int>) {
                    done(Result<GroupMember>::ok(member));
                });
            });
        });
    });
}

void GroupService::removeMember(const QString &groupId, const QString &userId, bool ban,
                                Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->removeMember(groupId, userId, ban, 0,
                        [this, groupId, userId, done = std::move(done)](
                            net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        if (!m_storage) {
            done(Result<bool>::ok(true));
            return;
        }
        auto future = m_storage->submit([groupId, userId](storage::Database &db) {
            return storage::GroupsRepo(db).removeMember(groupId, userId).isOk();
        });
        watch<bool>(this, future, [this, groupId, done](bool) {
            cleanupAfterMembershipChange(groupId, std::move(done));
        });
    });
}

void GroupService::leaveGroup(const QString &groupId, Done<bool> done)
{
    const QString selfId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    if (selfId.isEmpty()) {
        done(Result<bool>::error(QStringLiteral("self_identity_required")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->removeMember(groupId, selfId, false, 0,
                        [this, groupId, done = std::move(done)](
                            net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        removeLocalGroup(groupId, std::move(done));
    });
}

void GroupService::setRole(const QString &groupId, const QString &userId, GroupRole role,
                           quint32 permissions, Done<GroupMember> done)
{
    if (!m_api) {
        done(Result<GroupMember>::error(QStringLiteral("api_unavailable")));
        return;
    }
    const quint32 mask = role == GroupRole::Admin ? (permissions & group_permissions::All) : 0;
    m_api->setRole(groupId, userId, groupRoleToString(role), mask,
                   [this, groupId, role, done = std::move(done)](
                       net::ApiResult<net::GroupMemberInfo> result) {
        if (result.isError()) {
            done(Result<GroupMember>::error(apiError(result.error())));
            return;
        }
        const GroupMember member = memberFromInfo(result.value());
        if (!m_storage) {
            done(Result<GroupMember>::ok(member));
            return;
        }
        auto future = m_storage->submit([groupId, member](storage::Database &db) {
            return storage::GroupsRepo(db).upsertMember(groupId, member).isOk();
        });
        watch<bool>(this, future, [this, groupId, role, member, done](bool) {
            emit groupUpdated(groupId);
            if (role == GroupRole::Admin) {
                rotateSenderKey(groupId, [member, done](Result<bool>) {
                    done(Result<GroupMember>::ok(member));
                });
                return;
            }
            done(Result<GroupMember>::ok(member));
        });
    });
}

void GroupService::transferOwnership(const QString &groupId, const QString &userId,
                                     Done<Group> done)
{
    if (!m_api) {
        done(Result<Group>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->transferOwnership(groupId, userId,
                             [this, done = std::move(done)](
                                 net::ApiResult<net::GroupInfo> result) {
        if (result.isError()) {
            done(Result<Group>::error(apiError(result.error())));
            return;
        }
        persistGroup(result.value(), true, std::move(done));
    });
}

void GroupService::createInvite(const QString &groupId, Done<QString> done)
{
    if (!m_api) {
        done(Result<QString>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->createInvite(groupId, [this, groupId, done = std::move(done)](
                                     net::ApiResult<net::GroupInvite> result) {
        if (result.isError()) {
            done(Result<QString>::error(apiError(result.error())));
            return;
        }
        net::GroupInvite invite = result.value();
        if (invite.link.isEmpty() && !invite.token.isEmpty()) {
            invite.link = QStringLiteral("mini://g/") + invite.token;
        }
        if (!m_storage) {
            done(Result<QString>::ok(invite.link));
            return;
        }
        auto future = m_storage->submit(
            [groupId, invite](storage::Database &db) {
                storage::GroupsRepo repo(db);
                auto existing = repo.getGroup(groupId);
                if (existing.isError() || !existing.value().has_value()) {
                    return false;
                }
                Group group = existing.value().value();
                group.inviteToken = invite.token;
                group.inviteLink = invite.link;
                return repo.upsertGroup(group).isOk();
            });
        watch<bool>(this, future, [this, groupId, link = invite.link, done](bool saved) {
            if (saved) {
                emit groupUpdated(groupId);
            }
            done(Result<QString>::ok(link));
        });
    });
}

void GroupService::joinByToken(const QString &token, Done<Group> done)
{
    if (!m_api) {
        done(Result<Group>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->join(token, QString(),
                [this, done = std::move(done)](net::ApiResult<net::GroupJoinResult> result) {
        if (result.isError()) {
            done(Result<Group>::error(apiError(result.error())));
            return;
        }
        const net::GroupJoinResult joined = result.value();
        if (joined.status == QStringLiteral("pending")) {
            done(Result<Group>::error(QStringLiteral("join_pending")));
            return;
        }
        const QString groupId = joined.group.groupId;
        if (groupId.isEmpty()) {
            done(Result<Group>::error(QStringLiteral("group.invalid_response")));
            return;
        }
        auto finish = [this, done](Result<Group> saved) {
            if (saved.isError()) {
                done(saved);
                return;
            }
            const Group group = saved.value();
            ensureChat(group, [this, group, done](Result<bool>) {
                prepareGroup(group.groupId, [this, group, done](Result<bool> ready) {
                    if (ready.isError()) {
                        done(Result<Group>::ok(group));
                        return;
                    }
                    distributeSenderKey(group.groupId, [group, done](Result<int>) {
                        done(Result<Group>::ok(group));
                    });
                });
            });
        };
        if (joined.group.members.isEmpty()) {
            m_api->getGroup(groupId, [this, finish](net::ApiResult<net::GroupInfo> refreshed) {
                if (refreshed.isError()) {
                    finish(Result<Group>::error(apiError(refreshed.error())));
                    return;
                }
                persistGroup(refreshed.value(), true, finish);
            });
            return;
        }
        persistGroup(joined.group, true, finish);
    });
}

void GroupService::requestJoin(const QString &groupId, Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->requestJoin(groupId, QString(),
                       [done = std::move(done)](net::ApiResult<net::GroupJoinRequestInfo> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        done(Result<bool>::ok(true));
    });
}

void GroupService::listRequests(const QString &groupId, Done<QVector<GroupJoinRequest>> done)
{
    if (!m_api) {
        done(Result<QVector<GroupJoinRequest>>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->listRequests(groupId, QStringLiteral("pending"),
                        [done = std::move(done)](
                            net::ApiResult<QList<net::GroupJoinRequestInfo>> result) {
        if (result.isError()) {
            done(Result<QVector<GroupJoinRequest>>::error(apiError(result.error())));
            return;
        }
        QVector<GroupJoinRequest> requests;
        for (const net::GroupJoinRequestInfo &info : result.value()) {
            GroupJoinRequest request;
            request.groupId = info.groupId;
            request.userId = info.userId;
            request.login = info.login;
            request.displayName = info.displayName;
            request.message = info.message;
            request.status = info.status;
            request.createdAt = info.createdAt;
            requests.append(request);
        }
        done(Result<QVector<GroupJoinRequest>>::ok(requests));
    });
}

void GroupService::approveRequest(const QString &groupId, const QString &userId,
                                  bool approve, Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->approve(groupId, userId, approve,
                   [this, groupId, approve, done = std::move(done)](
                       net::ApiResult<net::GroupJoinRequestInfo> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        if (!approve) {
            done(Result<bool>::ok(true));
            return;
        }
        m_api->getGroup(groupId, [this, groupId, done](
                                     net::ApiResult<net::GroupInfo> refreshed) {
            if (refreshed.isError()) {
                done(Result<bool>::ok(true));
                return;
            }
            persistGroup(refreshed.value(), true, [this, groupId, done](Result<Group> saved) {
                if (saved.isError()) {
                    done(Result<bool>::ok(true));
                    return;
                }
                cleanupAfterMembershipChange(groupId, done);
            });
        });
    });
}

void GroupService::banMember(const QString &groupId, const QString &userId, bool permanent,
                             Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->ban(groupId, userId, permanent, 0, QString(),
               [this, groupId, userId, done = std::move(done)](
                   net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        if (!m_storage) {
            done(Result<bool>::ok(true));
            return;
        }
        auto future = m_storage->submit([groupId, userId](storage::Database &db) {
            return storage::GroupsRepo(db).removeMember(groupId, userId).isOk();
        });
        watch<bool>(this, future, [this, groupId, done](bool) {
            cleanupAfterMembershipChange(groupId, std::move(done));
        });
    });
}

void GroupService::unbanMember(const QString &groupId, const QString &userId, Done<bool> done)
{
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("api_unavailable")));
        return;
    }
    m_api->unban(groupId, userId, [done = std::move(done)](net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        done(Result<bool>::ok(true));
    });
}

void GroupService::cleanupAfterMembershipChange(const QString &groupId, Done<bool> done)
{
    rotateSenderKey(groupId, [this, groupId, done](Result<bool> rotated) {
        if (rotated.isError()) {
            done(rotated);
            return;
        }
        distributeSenderKey(groupId, [done](Result<int>) { done(Result<bool>::ok(true)); });
    });
}

void GroupService::loadLocalSenderKey(
    const QString &groupId,
    std::function<void(Result<crypto::SenderKeyState>)> done)
{
    if (m_localKeys.contains(groupId)) {
        done(Result<crypto::SenderKeyState>::ok(m_localKeys.value(groupId)));
        return;
    }
    if (!m_storage) {
        done(Result<crypto::SenderKeyState>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    auto future = m_storage->submit([groupId, senderId](storage::Database &db) {
        return storage::GroupsRepo(db).senderKey(groupId, senderId);
    });
    watch<Result<std::optional<crypto::SenderKeyState>>>(
        this, future,
        [this, groupId, done = std::move(done)](
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
            m_localKeys.insert(groupId, result.value().value());
            done(Result<crypto::SenderKeyState>::ok(result.value().value()));
        });
}

void GroupService::saveLocalSenderKey(const QString &groupId,
                                      const crypto::SenderKeyState &state,
                                      std::function<void(bool)> done)
{
    m_localKeys.insert(groupId, state);
    if (!m_storage) {
        if (done) {
            done(false);
        }
        return;
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    auto future = m_storage->submit([groupId, senderId, state](storage::Database &db) {
        return storage::GroupsRepo(db).upsertSenderKey(groupId, senderId, state).isOk();
    });
    if (done) {
        watch<bool>(this, future, [done = std::move(done)](bool saved) { done(saved); });
    }
}

void GroupService::prepareGroup(const QString &groupId,
                                std::function<void(Result<bool>)> done)
{
    if (m_localKeys.contains(groupId)) {
        done(Result<bool>::ok(true));
        return;
    }
    loadLocalSenderKey(groupId, [this, groupId, done](Result<crypto::SenderKeyState> loaded) {
        if (loaded.isOk()) {
            done(Result<bool>::ok(true));
            return;
        }
        auto generated = crypto::SenderKey::generate(1);
        if (generated.isError()) {
            done(Result<bool>::error(generated.error()));
            return;
        }
        saveLocalSenderKey(groupId, generated.value());
        done(Result<bool>::ok(true));
    });
}

bool GroupService::isGroupReady(const QString &groupId) const
{
    return m_localKeys.contains(groupId);
}

void GroupService::resolveSenderId(const QString &groupId, const QString &senderLogin,
                                   std::function<void(QString)> done)
{
    if (!m_storage) {
        done(QString());
        return;
    }
    auto future = m_storage->submit([groupId, senderLogin](storage::Database &db) {
        auto member = storage::GroupsRepo(db).memberByLogin(groupId, senderLogin);
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

void GroupService::prepareSenderKey(const QString &groupId, const QString &senderLogin,
                                    std::function<void(Result<bool>)> done)
{
    const QString key = peerKey(groupId, senderLogin);
    if (m_peerKeys.contains(key)) {
        done(Result<bool>::ok(true));
        return;
    }
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    resolveSenderId(groupId, senderLogin, [this, groupId, senderLogin, done](
                                             QString senderId) {
        auto future = m_storage->submit([groupId, senderId, senderLogin](storage::Database &db) {
            storage::GroupsRepo repo(db);
            auto stored = repo.senderKey(groupId, senderId);
            if (stored.isError()) {
                return stored;
            }
            if (stored.value().has_value()) {
                return stored;
            }
            if (senderId != senderLogin) {
                return repo.senderKey(groupId, senderLogin);
            }
            return stored;
        });
        watch<Result<std::optional<crypto::SenderKeyState>>>(
            this, future,
            [this, groupId, senderLogin, done](
                Result<std::optional<crypto::SenderKeyState>> result) {
                if (result.isError()) {
                    done(Result<bool>::error(result.error()));
                    return;
                }
                if (!result.value().has_value()) {
                    done(Result<bool>::error(QStringLiteral("sender_key_missing")));
                    return;
                }
                m_peerKeys.insert(peerKey(groupId, senderLogin), result.value().value());
                done(Result<bool>::ok(true));
            });
    });
}

Result<QByteArray> GroupService::encryptGroup(const QString &groupId,
                                              const QByteArray &plaintext,
                                              QJsonObject *header)
{
    auto it = m_localKeys.find(groupId);
    if (it == m_localKeys.end()) {
        return Result<QByteArray>::error(QStringLiteral("group_key_not_ready"));
    }
    const QString senderId = m_selfUserId.isEmpty() ? m_selfLogin : m_selfUserId;
    crypto::SenderKeyState &state = it.value();
    auto sealed = crypto::SenderKey::encrypt(state, groupId, senderId, plaintext, header);
    if (sealed.isError()) {
        return sealed;
    }
    const crypto::SenderKeyState snapshot = state;
    saveLocalSenderKey(groupId, snapshot);
    return sealed;
}

Result<QByteArray> GroupService::decryptGroup(const QString &groupId,
                                              const QString &senderLogin,
                                              const QByteArray &ciphertext,
                                              const QJsonObject &header)
{
    auto parsed = crypto::SenderKeyHeader::fromJson(header);
    if (parsed.isError()) {
        return Result<QByteArray>::error(parsed.error());
    }
    if (parsed.value().groupId != groupId) {
        return Result<QByteArray>::error(QStringLiteral("group_mismatch"));
    }
    const QString key = peerKey(groupId, senderLogin);
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
        auto future = m_storage->submit([groupId, senderId, snapshot](storage::Database &db) {
            return storage::GroupsRepo(db).upsertSenderKey(groupId, senderId, snapshot).isOk();
        });
        Q_UNUSED(future);
    }
    return plain;
}

void GroupService::rotateSenderKey(const QString &groupId, Done<bool> done)
{
    loadLocalSenderKey(groupId, [this, groupId, done](Result<crypto::SenderKeyState> loaded) {
        quint32 nextId = 1;
        if (loaded.isOk()) {
            nextId = loaded.value().keyId + 1;
        }
        auto generated = crypto::SenderKey::generate(nextId);
        if (generated.isError()) {
            done(Result<bool>::error(generated.error()));
            return;
        }
        saveLocalSenderKey(groupId, generated.value());
        emit groupUpdated(groupId);
        done(Result<bool>::ok(true));
    });
}

void GroupService::distributeSenderKey(const QString &groupId, Done<int> done)
{
    prepareGroup(groupId, [this, groupId, done](Result<bool> ready) {
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
            groupId, senderId, m_localKeys.value(groupId));
        auto future = m_storage->submit(
            [groupId](storage::Database &db) { return storage::GroupsRepo(db).members(groupId); });
        watch<Result<QVector<GroupMember>>>(
            this, future,
            [this, groupId, payload, done](Result<QVector<GroupMember>> result) {
                if (result.isError()) {
                    done(Result<int>::error(result.error()));
                    return;
                }
                auto state = std::make_shared<int>(0);
                auto remaining = std::make_shared<int>(0);
                for (const GroupMember &member : result.value()) {
                    if (member.login.isEmpty()
                        || member.login.compare(m_selfLogin, Qt::CaseInsensitive) == 0) {
                        continue;
                    }
                    ++(*remaining);
                    m_messages->sendControlMessage(
                        member.login, QStringLiteral("sender_key_distribution"), payload,
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

void GroupService::handleSenderKeyDistribution(const QString &groupId,
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
    const QString payloadSenderId =
        object.value(QStringLiteral("sender_id")).toString();

    resolveSenderId(groupId, senderLogin,
                    [this, groupId, senderLogin, payloadSenderId, state](
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
        const QString senderKey = peerKey(groupId, senderLogin);
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
            emit senderKeyStored(groupId, senderLogin);
            return;
        }
        const QString storedId = payloadSenderId.isEmpty() ? senderLogin
                                                          : payloadSenderId;
        auto future = m_storage->submit(
            [groupId, storedId, senderLogin, state](storage::Database &db) {
                storage::GroupsRepo repo(db);
                bool stored = repo.upsertSenderKey(groupId, storedId, state).isOk();
                if (senderLogin != storedId) {
                    stored = repo.upsertSenderKey(groupId, senderLogin, state).isOk()
                        && stored;
                }
                return stored;
            });
        watch<bool>(this, future, [this, groupId, senderLogin](bool) {
            emit senderKeyStored(groupId, senderLogin);
            emit groupUpdated(groupId);
        });
    });
}

} // namespace mini::services
