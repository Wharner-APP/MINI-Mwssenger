#include "services/ChatService.h"
#include "services/GroupService.h"
#include "services/MessageService.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/GroupsRepo.h"
#include "storage/StorageWorker.h"

#include "ServiceTestSupport.h"

#include <QHash>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <optional>

using namespace mini;
using namespace mini::services;
using namespace mini::services::test;

namespace {

class FakeGroupApi : public GroupApi
{
public:
    QHash<QString, net::GroupInfo> groups;
    QHash<QString, net::GroupJoinRequestInfo> requests;
    QString selfUserId = QStringLiteral("u_me");
    QString selfLogin = QStringLiteral("me");
    int created = 0;
    bool joinPending = false;

    static QString userIdFor(const QString &login)
    {
        return QStringLiteral("u_") + login;
    }

    void listGroups(GroupsDone done) override
    {
        QList<net::GroupInfo> list = groups.values();
        done(net::ApiResult<QList<net::GroupInfo>>::ok(list));
    }

    void createGroup(const net::GroupCreateRequest &request, GroupDone done) override
    {
        ++created;
        net::GroupInfo group;
        group.groupId = QStringLiteral("g_%1").arg(created);
        group.title = request.title;
        group.about = request.about;
        group.ownerId = selfUserId;
        group.isPublic = request.isPublic;
        group.joinByRequest = request.joinByRequest;
        group.myRole = QStringLiteral("owner");
        group.myPermissions = group_permissions::All;
        net::GroupMemberInfo owner;
        owner.userId = selfUserId;
        owner.login = selfLogin;
        owner.role = QStringLiteral("owner");
        owner.permissions = group_permissions::All;
        group.members.append(owner);
        for (const QString &login : request.members) {
            net::GroupMemberInfo member;
            member.userId = userIdFor(login);
            member.login = login;
            member.role = QStringLiteral("member");
            group.members.append(member);
        }
        group.memberCount = group.members.size();
        group.inviteToken = QStringLiteral("tok_") + group.groupId;
        group.inviteLink = QStringLiteral("mini://g/") + group.inviteToken;
        groups.insert(group.groupId, group);
        done(net::ApiResult<net::GroupInfo>::ok(group));
    }

    void getGroup(const QString &groupId, GroupDone done) override
    {
        if (!groups.contains(groupId)) {
            done(net::ApiResult<net::GroupInfo>::fail(
                net::ApiError::protocol(QStringLiteral("group_not_found"))));
            return;
        }
        done(net::ApiResult<net::GroupInfo>::ok(groups.value(groupId)));
    }

    void updateGroup(const QString &groupId, const net::GroupPatch &patch,
                     GroupDone done) override
    {
        Q_UNUSED(patch);
        getGroup(groupId, done);
    }

    void addMember(const QString &groupId, const QString &userIdOrLogin,
                   MemberDone done) override
    {
        net::GroupInfo &group = groups[groupId];
        net::GroupMemberInfo member;
        member.userId = userIdOrLogin.startsWith(QLatin1String("u_"))
            ? userIdOrLogin
            : userIdFor(userIdOrLogin);
        member.login = userIdOrLogin.startsWith(QLatin1String("u_"))
            ? userIdOrLogin.mid(2)
            : userIdOrLogin;
        member.role = QStringLiteral("member");
        group.members.append(member);
        group.memberCount = group.members.size();
        done(net::ApiResult<net::GroupMemberInfo>::ok(member));
    }

    void removeMember(const QString &groupId, const QString &userId, bool ban,
                      int durationHours, EmptyDone done) override
    {
        Q_UNUSED(ban);
        Q_UNUSED(durationHours);
        net::GroupInfo &group = groups[groupId];
        for (int i = 0; i < group.members.size(); ++i) {
            if (group.members.at(i).userId == userId) {
                group.members.removeAt(i);
                break;
            }
        }
        group.memberCount = group.members.size();
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void setRole(const QString &groupId, const QString &userId, const QString &role,
                 quint32 permissions, MemberDone done) override
    {
        net::GroupMemberInfo found;
        for (net::GroupMemberInfo &member : groups[groupId].members) {
            if (member.userId == userId) {
                member.role = role;
                member.permissions = permissions;
                found = member;
            }
        }
        done(net::ApiResult<net::GroupMemberInfo>::ok(found));
    }

    void transferOwnership(const QString &groupId, const QString &userId,
                           GroupDone done) override
    {
        for (net::GroupMemberInfo &member : groups[groupId].members) {
            if (member.userId == userId) {
                member.role = QStringLiteral("owner");
                member.permissions = group_permissions::All;
            } else if (member.userId == groups[groupId].ownerId) {
                member.role = QStringLiteral("admin");
                member.permissions = group_permissions::All;
            }
        }
        groups[groupId].ownerId = userId;
        getGroup(groupId, done);
    }

    void createInvite(const QString &groupId, InviteDone done) override
    {
        net::GroupInvite invite;
        invite.groupId = groupId;
        invite.token = QStringLiteral("new_token");
        invite.link = QStringLiteral("mini://g/new_token");
        groups[groupId].inviteToken = invite.token;
        groups[groupId].inviteLink = invite.link;
        done(net::ApiResult<net::GroupInvite>::ok(invite));
    }

    void join(const QString &token, const QString &message, JoinDone done) override
    {
        Q_UNUSED(token);
        Q_UNUSED(message);
        net::GroupJoinResult result;
        result.status = joinPending ? QStringLiteral("pending") : QStringLiteral("joined");
        if (!joinPending && !groups.isEmpty()) {
            result.group = groups.values().first();
        }
        done(net::ApiResult<net::GroupJoinResult>::ok(result));
    }

    void requestJoin(const QString &groupId, const QString &message,
                     RequestDone done) override
    {
        net::GroupJoinRequestInfo request;
        request.groupId = groupId;
        request.userId = selfUserId;
        request.login = selfLogin;
        request.message = message;
        requests.insert(groupId, request);
        done(net::ApiResult<net::GroupJoinRequestInfo>::ok(request));
    }

    void listRequests(const QString &groupId, const QString &status,
                      RequestsDone done) override
    {
        Q_UNUSED(status);
        QList<net::GroupJoinRequestInfo> list;
        if (requests.contains(groupId)) {
            list.append(requests.value(groupId));
        }
        done(net::ApiResult<QList<net::GroupJoinRequestInfo>>::ok(list));
    }

    void approve(const QString &groupId, const QString &userId, bool approve,
                 RequestDone done) override
    {
        Q_UNUSED(userId);
        net::GroupJoinRequestInfo request = requests.value(groupId);
        request.status = approve ? QStringLiteral("approved") : QStringLiteral("rejected");
        done(net::ApiResult<net::GroupJoinRequestInfo>::ok(request));
    }

    void ban(const QString &groupId, const QString &userId, bool permanent,
             int durationHours, const QString &reason, EmptyDone done) override
    {
        Q_UNUSED(permanent);
        Q_UNUSED(durationHours);
        Q_UNUSED(reason);
        removeMember(groupId, userId, true, 0,
                     [done](net::ApiResult<net::Empty> result) { done(result); });
    }

    void unban(const QString &groupId, const QString &userId, EmptyDone done) override
    {
        Q_UNUSED(groupId);
        Q_UNUSED(userId);
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }
};

class StubCrypto : public CryptoSession
{
public:
    Result<QByteArray> encrypt(const QString &, const QByteArray &plaintext,
                               QJsonObject *) override
    {
        return Result<QByteArray>::ok(plaintext);
    }
    Result<QByteArray> decrypt(const QString &, const QByteArray &ciphertext,
                               const QJsonObject &) override
    {
        return Result<QByteArray>::ok(ciphertext);
    }
};

class StubTransport : public MessageTransport
{
public:
    QList<net::Envelope> envelopes;

    void send(const QList<net::Envelope> &items, SendCallback done) override
    {
        envelopes.append(items);
        net::SendResult result;
        for (const net::Envelope &item : items) {
            result.accepted.append(item.clientMsgId);
        }
        if (done) {
            done(net::ApiResult<net::SendResult>::ok(result));
        }
    }

    void deliver(const net::Envelope &envelope) { emit messageReceived(envelope); }

    [[nodiscard]] QList<net::Envelope> ofKind(const QString &kind) const
    {
        QList<net::Envelope> matching;
        for (const net::Envelope &envelope : envelopes) {
            if (envelope.kind == kind) {
                matching.append(envelope);
            }
        }
        return matching;
    }
};

std::optional<Group> groupInDb(storage::StorageWorker &worker, const QString &groupId)
{
    auto future = worker.submit(
        [groupId](storage::Database &db) { return storage::GroupsRepo(db).getGroup(groupId); });
    future.waitForFinished();
    const auto result = future.result();
    if (result.isError() || !result.value().has_value()) {
        return std::nullopt;
    }
    return result.value().value();
}

QVector<GroupMember> membersInDb(storage::StorageWorker &worker, const QString &groupId)
{
    auto future = worker.submit(
        [groupId](storage::Database &db) { return storage::GroupsRepo(db).members(groupId); });
    future.waitForFinished();
    return future.result().isOk() ? future.result().value() : QVector<GroupMember>();
}

std::optional<crypto::SenderKeyState> senderKeyInDb(storage::StorageWorker &worker,
                                                    const QString &groupId,
                                                    const QString &senderId)
{
    auto future = worker.submit([groupId, senderId](storage::Database &db) {
        return storage::GroupsRepo(db).senderKey(groupId, senderId);
    });
    future.waitForFinished();
    const auto result = future.result();
    if (result.isError() || !result.value().has_value()) {
        return std::nullopt;
    }
    return result.value().value();
}

} // namespace

class TestGroupService : public QObject
{
    Q_OBJECT

private slots:
    void validateDraftRules();
    void createsGroupPersistsRosterAndDistributesKey();
    void groupTextRoundTripBetweenTwoMembers();
    void rolesInviteRequestAndBan();
    void removalRotatesSenderKey();
};

void TestGroupService::validateDraftRules()
{
    GroupDraft empty;
    QVERIFY(GroupService::validateDraft(empty).isError());

    GroupDraft noMembers;
    noMembers.title = QStringLiteral("Team");
    QVERIFY(GroupService::validateDraft(noMembers).isError());

    GroupDraft ok;
    ok.title = QStringLiteral("  Team  ");
    ok.members = {QStringLiteral("alice"), QStringLiteral(" alice "),
                  QStringLiteral("bob")};
    const auto validated = GroupService::validateDraft(ok);
    QVERIFY(validated.isOk());
    QCOMPARE(validated.value().title, QStringLiteral("Team"));
    QCOMPARE(validated.value().members.size(), 2);
}

void TestGroupService::createsGroupPersistsRosterAndDistributesKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    FakeGroupApi api;
    GroupService groups(&worker);
    groups.setApi(&api);
    groups.setSelfIdentity(api.selfUserId, api.selfLogin);
    MessageService messages(&worker);
    messages.setSelfLogin(api.selfLogin);
    messages.setCancelWindowMs(0);
    StubCrypto crypto;
    StubTransport transport;
    messages.setCryptoSession(&crypto);
    messages.setTransport(&transport);
    groups.setMessageService(&messages);

    GroupDraft draft;
    draft.title = QStringLiteral("Team");
    draft.members = {QStringLiteral("alice")};

    Group created;
    bool done = false;
    groups.createGroup(draft, [&](Result<Group> result) {
        QVERIFY2(result.isOk(), qPrintable(result.error()));
        created = result.value();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(created.groupId, QStringLiteral("g_1"));
    QCOMPARE(created.myRole, GroupRole::Owner);
    QCOMPARE(created.members.size(), 2);

    QTRY_VERIFY_WITH_TIMEOUT(groupInDb(worker, created.groupId).has_value(), 3000);
    QCOMPARE(membersInDb(worker, created.groupId).size(), 2);
    QVERIFY(fetchChat(worker, group_ids::chatId(created.groupId)).has_value());
    QCOMPARE(fetchChat(worker, group_ids::chatId(created.groupId))->kind,
             QStringLiteral("group"));

    QTRY_VERIFY_WITH_TIMEOUT(!transport.ofKind(QStringLiteral("sender_key_distribution")).isEmpty(),
                             5000);
    const net::Envelope distribution =
        transport.ofKind(QStringLiteral("sender_key_distribution")).first();
    QCOMPARE(distribution.chatId,
             ChatService::directChatId(api.selfLogin, QStringLiteral("alice")));
    QVERIFY(!distribution.ciphertext.isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(
        senderKeyInDb(worker, created.groupId, api.selfUserId).has_value(), 3000);
    QCOMPARE(senderKeyInDb(worker, created.groupId, api.selfUserId)->keyId, 1u);

    QVector<Group> listed;
    groups.listGroups([&](Result<QVector<Group>> result) {
        listed = result.isOk() ? result.value() : QVector<Group>();
    });
    QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 3000);
}

void TestGroupService::groupTextRoundTripBetweenTwoMembers()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());
    storage::StorageWorker workerA;
    storage::StorageWorker workerB;
    QVERIFY(workerA.open(dirA.filePath(QStringLiteral("a.sqlite")), dbKey()).isOk());
    QVERIFY(workerB.open(dirB.filePath(QStringLiteral("b.sqlite")), dbKey()).isOk());

    FakeGroupApi api;
    GroupService sender(&workerA);
    sender.setApi(&api);
    sender.setSelfIdentity(api.selfUserId, api.selfLogin);
    MessageService senderMessages(&workerA);
    senderMessages.setSelfLogin(api.selfLogin);
    senderMessages.setCancelWindowMs(0);
    StubCrypto senderCrypto;
    StubTransport senderTransport;
    senderMessages.setCryptoSession(&senderCrypto);
    senderMessages.setTransport(&senderTransport);
    sender.setMessageService(&senderMessages);
    senderMessages.setGroupCrypto(&sender);

    Group created;
    bool createdDone = false;
    GroupDraft draft;
    draft.title = QStringLiteral("Team");
    draft.members = {QStringLiteral("alice")};
    sender.createGroup(draft, [&](Result<Group> result) {
        created = result.isOk() ? result.value() : Group{};
        createdDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(createdDone && created.isValid(), 5000);

    const QString chatId = group_ids::chatId(created.groupId);
    const QString outgoingId = senderMessages.sendText(chatId, QStringLiteral("hello group"));
    QVERIFY(!outgoingId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!senderTransport.ofKind(QStringLiteral("group_text")).isEmpty(),
                             5000);
    const net::Envelope groupEnvelope =
        senderTransport.ofKind(QStringLiteral("group_text")).first();
    QCOMPARE(groupEnvelope.chatId, chatId);
    QCOMPARE(groupEnvelope.header.value(QStringLiteral("group_id")).toString(),
             created.groupId);
    QVERIFY(!groupEnvelope.header.value(QStringLiteral("sig")).toString().isEmpty());
    QVERIFY(groupEnvelope.header.value(QStringLiteral("key_id")).toInt() >= 1);

    // Receiver: same group roster, sender key from the distribution envelope.
    GroupService receiver(&workerB);
    receiver.setApi(&api);
    receiver.setSelfIdentity(QStringLiteral("u_alice"), QStringLiteral("alice"));
    MessageService receiverMessages(&workerB);
    receiverMessages.setSelfLogin(QStringLiteral("alice"));
    receiverMessages.setCancelWindowMs(0);
    StubCrypto receiverCrypto;
    StubTransport receiverTransport;
    receiverMessages.setCryptoSession(&receiverCrypto);
    receiverMessages.setTransport(&receiverTransport);
    receiver.setMessageService(&receiverMessages);
    receiverMessages.setGroupCrypto(&receiver);

    bool loaded = false;
    receiver.loadGroup(created.groupId, [&](Result<Group> result) { loaded = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(loaded, 5000);

    const net::Envelope distribution =
        senderTransport.ofKind(QStringLiteral("sender_key_distribution")).first();
    QSignalSpy stored(&receiver, &GroupService::senderKeyStored);
    receiver.handleSenderKeyDistribution(created.groupId, api.selfLogin,
                                         distribution.ciphertext);
    QTRY_VERIFY_WITH_TIMEOUT(stored.count() >= 1, 3000);

    QSignalSpy added(&receiverMessages, &MessageService::messageAdded);
    net::Envelope incoming = groupEnvelope;
    incoming.header.insert(QStringLiteral("sender_login"), api.selfLogin);
    receiverTransport.deliver(incoming);
    QTRY_VERIFY_WITH_TIMEOUT(added.count() >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(messageCount(workerB, chatId), 1, 5000);
    const auto storedMessage = fetchMessage(workerB, chatId, incoming.clientMsgId);
    QVERIFY(storedMessage.has_value());
    QCOMPARE(storedMessage->body, QStringLiteral("hello group"));
    QCOMPARE(storedMessage->senderLogin, api.selfLogin);
    QCOMPARE(fetchChat(workerB, chatId)->kind, QStringLiteral("group"));
}

void TestGroupService::rolesInviteRequestAndBan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    FakeGroupApi api;
    GroupService groups(&worker);
    groups.setApi(&api);
    groups.setSelfIdentity(api.selfUserId, api.selfLogin);

    GroupDraft draft;
    draft.title = QStringLiteral("Team");
    draft.members = {QStringLiteral("alice"), QStringLiteral("bob")};
    Group created;
    bool done = false;
    groups.createGroup(draft, [&](Result<Group> result) {
        created = result.isOk() ? result.value() : Group{};
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done && created.isValid(), 5000);

    bool roleDone = false;
    GroupMember admin;
    groups.setRole(created.groupId, QStringLiteral("u_alice"), GroupRole::Admin,
                   group_permissions::Ban | group_permissions::Invite,
                   [&](Result<GroupMember> result) {
                       admin = result.isOk() ? result.value() : GroupMember{};
                       roleDone = true;
                   });
    QTRY_VERIFY_WITH_TIMEOUT(roleDone, 3000);
    QCOMPARE(admin.role, GroupRole::Admin);
    QCOMPARE(admin.permissions & group_permissions::Ban, group_permissions::Ban);

    bool inviteDone = false;
    QString link;
    groups.createInvite(created.groupId, [&](Result<QString> result) {
        link = result.isOk() ? result.value() : QString();
        inviteDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(inviteDone, 3000);
    QVERIFY(link.startsWith(QStringLiteral("mini://g/")));

    bool requestDone = false;
    groups.requestJoin(created.groupId, [&](Result<bool> result) { requestDone = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(requestDone, 3000);
    QVector<GroupJoinRequest> requests;
    bool listed = false;
    groups.listRequests(created.groupId, [&](Result<QVector<GroupJoinRequest>> result) {
        requests = result.isOk() ? result.value() : QVector<GroupJoinRequest>();
        listed = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(listed, 3000);
    QCOMPARE(requests.size(), 1);

    bool approveDone = false;
    groups.approveRequest(created.groupId, requests.first().userId, true,
                          [&](Result<bool> result) { approveDone = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(approveDone, 3000);

    bool banDone = false;
    groups.banMember(created.groupId, QStringLiteral("u_bob"), true,
                     [&](Result<bool> result) { banDone = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(banDone, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(membersInDb(worker, created.groupId).size() == 2, 3000);

    bool transferDone = false;
    Group transferred;
    groups.transferOwnership(created.groupId, QStringLiteral("u_alice"),
                             [&](Result<Group> result) {
                                 transferred = result.isOk() ? result.value() : Group{};
                                 transferDone = result.isOk();
                             });
    QTRY_VERIFY_WITH_TIMEOUT(transferDone, 5000);
    QCOMPARE(transferred.ownerId, QStringLiteral("u_alice"));

    bool leaveDone = false;
    groups.leaveGroup(created.groupId, [&](Result<bool> result) { leaveDone = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(leaveDone, 5000);
    QTRY_VERIFY(!groupInDb(worker, created.groupId).has_value());
    QTRY_VERIFY(!fetchChat(worker, group_ids::chatId(created.groupId)).has_value());
}

void TestGroupService::removalRotatesSenderKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    FakeGroupApi api;
    GroupService groups(&worker);
    groups.setApi(&api);
    groups.setSelfIdentity(api.selfUserId, api.selfLogin);

    GroupDraft draft;
    draft.title = QStringLiteral("Team");
    draft.members = {QStringLiteral("alice")};
    Group created;
    bool done = false;
    groups.createGroup(draft, [&](Result<Group> result) {
        created = result.isOk() ? result.value() : Group{};
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done && created.isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        senderKeyInDb(worker, created.groupId, api.selfUserId).has_value(), 3000);
    const quint32 before = senderKeyInDb(worker, created.groupId, api.selfUserId)->keyId;

    bool removed = false;
    groups.removeMember(created.groupId, QStringLiteral("u_alice"), false,
                        [&](Result<bool> result) { removed = result.isOk(); });
    QTRY_VERIFY_WITH_TIMEOUT(removed, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        senderKeyInDb(worker, created.groupId, api.selfUserId).has_value()
            && senderKeyInDb(worker, created.groupId, api.selfUserId)->keyId == before + 1,
        5000);
    QCOMPARE(membersInDb(worker, created.groupId).size(), 1);
}

QTEST_MAIN(TestGroupService)
#include "test_group_service.moc"
