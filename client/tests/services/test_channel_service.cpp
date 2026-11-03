#include "services/ChannelService.h"
#include "services/ChatService.h"
#include "services/MessageService.h"
#include "storage/ChannelsRepo.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
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

// Shared in-memory channel directory: two clients (owner and subscriber)
// talk to the same store but each sees its own my_role.
struct ChannelStore
{
    QHash<QString, net::ChannelInfo> channels;
    QHash<QString, QVector<net::ChannelMemberInfo>> members;
    QStringList tokens;
    int created = 0;
    int views = 0;
};

class FakeChannelApi : public ChannelApi
{
public:
    FakeChannelApi(ChannelStore *store, QString login)
        : m_store(store), m_login(std::move(login)), m_userId(QStringLiteral("u_") + m_login)
    {
    }

    static net::ChannelMemberInfo memberFor(const QString &login, const QString &role,
                                            quint32 permissions = 0)
    {
        net::ChannelMemberInfo member;
        member.userId = QStringLiteral("u_") + login;
        member.login = login;
        member.role = role;
        member.permissions = permissions;
        member.joinedAt = 1;
        return member;
    }

    void addMember(const QString &channelId, const net::ChannelMemberInfo &member)
    {
        QVector<net::ChannelMemberInfo> &list = m_store->members[channelId];
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).login == member.login) {
                list[i] = member;
                return;
            }
        }
        list.append(member);
    }

    void listChannels(ChannelsDone done) override
    {
        QList<net::ChannelInfo> list;
        for (const net::ChannelInfo &stored : m_store->channels) {
            list.append(view(stored.channelId).value());
        }
        done(net::ApiResult<QList<net::ChannelInfo>>::ok(list));
    }

    void createChannel(const net::ChannelCreateRequest &request, ChannelDone done) override
    {
        ++m_store->created;
        net::ChannelInfo channel;
        channel.channelId = QStringLiteral("c_%1").arg(m_store->created);
        channel.title = request.title;
        channel.about = request.about;
        channel.ownerId = m_userId;
        channel.isPublic = request.isPublic;
        channel.linkedGroup = request.linkedGroup;
        channel.createdAt = 1000;
        channel.updatedAt = 1000;
        channel.inviteToken = QStringLiteral("tok_") + channel.channelId;
        channel.inviteLink = QStringLiteral("mini://c/%1/%2")
                                 .arg(channel.channelId, channel.inviteToken);
        m_store->channels.insert(channel.channelId, channel);
        addMember(channel.channelId,
                  memberFor(m_login, QStringLiteral("owner"), channel_permissions::All));
        done(net::ApiResult<net::ChannelInfo>::ok(view(channel.channelId).value()));
    }

    void getChannel(const QString &channelId, ChannelDone done) override
    {
        done(view(channelId));
    }

    void updateChannel(const QString &channelId, const net::ChannelPatch &patch,
                       ChannelDone done) override
    {
        if (!m_store->channels.contains(channelId)) {
            done(missing());
            return;
        }
        net::ChannelInfo &channel = m_store->channels[channelId];
        if (patch.title.has_value()) {
            channel.title = *patch.title;
        }
        if (patch.about.has_value()) {
            channel.about = *patch.about;
        }
        if (patch.isPublic.has_value()) {
            channel.isPublic = *patch.isPublic;
        }
        done(view(channelId));
    }

    void deleteChannel(const QString &channelId, EmptyDone done) override
    {
        m_store->channels.remove(channelId);
        m_store->members.remove(channelId);
        done(net::ApiResult<net::Empty>::ok(net::Empty()));
    }

    void subscribe(const QString &channelId, const QString &token, StatusDone done) override
    {
        if (!m_store->channels.contains(channelId)) {
            done(net::ApiResult<QString>::fail(
                net::ApiError::protocol(QStringLiteral("channel_not_found"))));
            return;
        }
        const net::ChannelInfo &channel = m_store->channels.value(channelId);
        if (!channel.isPublic && token != channel.inviteToken) {
            done(net::ApiResult<QString>::fail(
                net::ApiError::protocol(QStringLiteral("invite_invalid"))));
            return;
        }
        addMember(channelId, memberFor(m_login, QStringLiteral("subscriber")));
        done(net::ApiResult<QString>::ok(QStringLiteral("subscribed")));
    }

    void unsubscribe(const QString &channelId, StatusDone done) override
    {
        QVector<net::ChannelMemberInfo> &list = m_store->members[channelId];
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).login == m_login) {
                list.removeAt(i);
                break;
            }
        }
        done(net::ApiResult<QString>::ok(QStringLiteral("unsubscribed")));
    }

    void members(const QString &channelId, MembersDone done) override
    {
        QList<net::ChannelMemberInfo> list;
        for (const net::ChannelMemberInfo &member : m_store->members.value(channelId)) {
            list.append(member);
        }
        done(net::ApiResult<QList<net::ChannelMemberInfo>>::ok(list));
    }

    void setRole(const QString &channelId, const QString &userId, const QString &role,
                 quint32 permissions, MemberDone done) override
    {
        QVector<net::ChannelMemberInfo> &list = m_store->members[channelId];
        for (net::ChannelMemberInfo &member : list) {
            if (member.userId == userId || member.login == userId) {
                member.role = role;
                member.permissions = permissions;
                done(net::ApiResult<net::ChannelMemberInfo>::ok(member));
                return;
            }
        }
        done(net::ApiResult<net::ChannelMemberInfo>::fail(
            net::ApiError::protocol(QStringLiteral("not_member"))));
    }

    void createInvite(const QString &channelId, InviteDone done) override
    {
        net::ChannelInvite invite;
        invite.channelId = channelId;
        invite.token = QStringLiteral("tok_") + channelId;
        invite.link = QStringLiteral("mini://c/%1/%2").arg(channelId, invite.token);
        done(net::ApiResult<net::ChannelInvite>::ok(invite));
    }

    void recordView(const QString &channelId, const QString &postId, ViewDone done) override
    {
        ++m_store->views;
        net::ChannelViewInfo view;
        view.status = QStringLiteral("ok");
        view.channelId = channelId;
        view.postId = postId;
        view.recorded = true;
        view.views = m_store->views;
        done(net::ApiResult<net::ChannelViewInfo>::ok(view));
    }

private:
    static net::ApiResult<net::ChannelInfo> missing()
    {
        return net::ApiResult<net::ChannelInfo>::fail(
            net::ApiError::protocol(QStringLiteral("channel_not_found")));
    }

    net::ApiResult<net::ChannelInfo> view(const QString &channelId)
    {
        if (!m_store->channels.contains(channelId)) {
            return missing();
        }
        net::ChannelInfo channel = m_store->channels.value(channelId);
        channel.members = m_store->members.value(channelId).toList();
        channel.myRole.clear();
        channel.myPermissions = 0;
        for (const net::ChannelMemberInfo &member : m_store->members.value(channelId)) {
            if (member.login == m_login) {
                channel.myRole = member.role;
                channel.myPermissions = member.permissions;
                break;
            }
        }
        channel.subscriberCount = m_store->members.value(channelId).size();
        return net::ApiResult<net::ChannelInfo>::ok(channel);
    }

    ChannelStore *m_store = nullptr;
    QString m_login;
    QString m_userId;
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

std::optional<Channel> channelInDb(storage::StorageWorker &worker, const QString &channelId)
{
    auto future = worker.submit([channelId](storage::Database &db) {
        return storage::ChannelsRepo(db).getChannel(channelId);
    });
    future.waitForFinished();
    const auto result = future.result();
    if (result.isError() || !result.value().has_value()) {
        return std::nullopt;
    }
    return result.value().value();
}

QVector<ChannelMember> membersInDb(storage::StorageWorker &worker, const QString &channelId)
{
    auto future = worker.submit([channelId](storage::Database &db) {
        return storage::ChannelsRepo(db).members(channelId);
    });
    future.waitForFinished();
    return future.result().isOk() ? future.result().value() : QVector<ChannelMember>();
}

} // namespace

class TestChannelService : public QObject
{
    Q_OBJECT

private slots:
    void validateDraftRules();
    void createsChannelPersistsRosterAndSenderKey();
    void publicationReachesSubscriber();
    void viewsAndInviteUseTheServer();
};

void TestChannelService::validateDraftRules()
{
    ChannelDraft empty;
    QVERIFY(ChannelService::validateDraft(empty).isError());

    ChannelDraft tooLong;
    tooLong.title = QString(129, QLatin1Char('x'));
    QVERIFY(ChannelService::validateDraft(tooLong).isError());

    ChannelDraft about;
    about.title = QStringLiteral("News");
    about.about = QString(2001, QLatin1Char('x'));
    QVERIFY(ChannelService::validateDraft(about).isError());

    ChannelDraft ok;
    ok.title = QStringLiteral("  News  ");
    const auto valid = ChannelService::validateDraft(ok);
    QVERIFY(valid.isOk());
    QCOMPARE(valid.value().title, QStringLiteral("News"));
}

void TestChannelService::createsChannelPersistsRosterAndSenderKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ChannelStore store;
    FakeChannelApi api(&store, QStringLiteral("me"));
    ChannelService channels(&worker);
    channels.setApi(&api);
    channels.setSelfIdentity(QStringLiteral("u_me"), QStringLiteral("me"));

    MessageService messages(&worker);
    messages.setSelfLogin(QStringLiteral("me"));
    messages.setCancelWindowMs(0);
    StubCrypto crypto;
    StubTransport transport;
    messages.setCryptoSession(&crypto);
    messages.setTransport(&transport);
    channels.setMessageService(&messages);
    messages.setChannelCrypto(&channels);

    ChannelDraft draft;
    draft.title = QStringLiteral("News");
    draft.about = QStringLiteral("Daily updates");
    draft.isPublic = true;

    Channel created;
    bool done = false;
    channels.createChannel(draft, [&](Result<Channel> result) {
        QVERIFY2(result.isOk(), qPrintable(result.error()));
        created = result.value();
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(created.myRole == ChannelRole::Owner);
    QVERIFY(created.isMember);
    QVERIFY(created.canPublish());
    QVERIFY(created.isPublic);

    QTRY_VERIFY_WITH_TIMEOUT(channelInDb(worker, created.channelId).has_value(), 3000);
    QCOMPARE(membersInDb(worker, created.channelId).size(), 1);
    QVERIFY(fetchChat(worker, channel_ids::chatId(created.channelId)).has_value());
    QCOMPARE(fetchChat(worker, channel_ids::chatId(created.channelId))->kind,
             QStringLiteral("channel"));

    QTRY_VERIFY_WITH_TIMEOUT(
        channels.isGroupReady(created.channelId), 3000);
    auto future = worker.submit([&created](storage::Database &db) {
        return storage::ChannelsRepo(db).senderKey(created.channelId,
                                                   QStringLiteral("u_me"));
    });
    future.waitForFinished();
    QVERIFY(future.result().isOk() && future.result().value().has_value());
    QCOMPARE(future.result().value()->keyId, 1u);

    QVector<Channel> listed;
    channels.listChannels([&](Result<QVector<Channel>> result) {
        listed = result.isOk() ? result.value() : QVector<Channel>();
    });
    QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 3000);
}

void TestChannelService::publicationReachesSubscriber()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());
    storage::StorageWorker workerA;
    storage::StorageWorker workerB;
    QVERIFY(workerA.open(dirA.filePath(QStringLiteral("a.sqlite")), dbKey()).isOk());
    QVERIFY(workerB.open(dirB.filePath(QStringLiteral("b.sqlite")), dbKey()).isOk());

    ChannelStore store;
    FakeChannelApi adminApi(&store, QStringLiteral("me"));
    FakeChannelApi userApi(&store, QStringLiteral("alice"));

    ChannelService admin(&workerA);
    admin.setApi(&adminApi);
    admin.setSelfIdentity(QStringLiteral("u_me"), QStringLiteral("me"));
    MessageService adminMessages(&workerA);
    adminMessages.setSelfLogin(QStringLiteral("me"));
    adminMessages.setCancelWindowMs(0);
    StubCrypto adminCrypto;
    StubTransport adminTransport;
    adminMessages.setCryptoSession(&adminCrypto);
    adminMessages.setTransport(&adminTransport);
    admin.setMessageService(&adminMessages);
    adminMessages.setChannelCrypto(&admin);

    Channel created;
    bool createdDone = false;
    ChannelDraft draft;
    draft.title = QStringLiteral("News");
    draft.isPublic = false;
    admin.createChannel(draft, [&](Result<Channel> result) {
        created = result.isOk() ? result.value() : Channel{};
        createdDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(createdDone && created.isValid(), 5000);

    // The subscriber joins with the invite token (private channel).
    ChannelService subscriber(&workerB);
    subscriber.setApi(&userApi);
    subscriber.setSelfIdentity(QStringLiteral("u_alice"), QStringLiteral("alice"));
    MessageService userMessages(&workerB);
    userMessages.setSelfLogin(QStringLiteral("alice"));
    userMessages.setCancelWindowMs(0);
    StubCrypto userCrypto;
    StubTransport userTransport;
    userMessages.setCryptoSession(&userCrypto);
    userMessages.setTransport(&userTransport);
    subscriber.setMessageService(&userMessages);
    userMessages.setChannelCrypto(&subscriber);

    Channel joined;
    bool joinedDone = false;
    subscriber.subscribe(created.channelId, QStringLiteral("tok_") + created.channelId,
                         [&](Result<Channel> result) {
                             joined = result.isOk() ? result.value() : Channel{};
                             joinedDone = true;
                         });
    QTRY_VERIFY_WITH_TIMEOUT(joinedDone && joined.isValid(), 5000);
    QVERIFY(joined.isMember);
    QVERIFY(joined.myRole == ChannelRole::Subscriber);
    QVERIFY(!joined.canPublish());
    QVERIFY(fetchChat(workerB, channel_ids::chatId(created.channelId)).has_value());

    // The owner refreshes the roster; this distributes the current sender
    // key to the new subscriber over the pairwise stub transport.
    Channel refreshed;
    bool refreshedDone = false;
    admin.loadChannel(created.channelId, [&](Result<Channel> result) {
        refreshed = result.isOk() ? result.value() : Channel{};
        refreshedDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(refreshedDone && refreshed.isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !adminTransport.ofKind(QStringLiteral("channel_sender_key")).isEmpty(), 5000);
    const net::Envelope distribution =
        adminTransport.ofKind(QStringLiteral("channel_sender_key")).first();
    QCOMPARE(distribution.header.value(QStringLiteral("to")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(distribution.chatId,
             ChatService::directChatId(QStringLiteral("me"), QStringLiteral("alice")));

    QSignalSpy stored(&subscriber, &ChannelService::senderKeyStored);
    subscriber.handleSenderKeyDistribution(created.channelId, QStringLiteral("me"),
                                           distribution.ciphertext);
    QTRY_VERIFY_WITH_TIMEOUT(stored.count() >= 1, 3000);

    // Publish: one envelope per subscriber, routed by header.to (§43.2).
    const QString chatId = channel_ids::chatId(created.channelId);
    const QString outgoingId = adminMessages.sendText(chatId, QStringLiteral("First post"));
    QVERIFY(!outgoingId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!adminTransport.ofKind(QStringLiteral("channel_text")).isEmpty(),
                             5000);
    const QList<net::Envelope> posts = adminTransport.ofKind(QStringLiteral("channel_text"));
    QCOMPARE(posts.size(), 1);
    const net::Envelope post = posts.first();
    QCOMPARE(post.chatId, chatId);
    QCOMPARE(post.header.value(QStringLiteral("to")).toString(), QStringLiteral("alice"));
    QCOMPARE(post.header.value(QStringLiteral("group_id")).toString(), created.channelId);
    QVERIFY(!post.header.value(QStringLiteral("sig")).toString().isEmpty());

    QSignalSpy added(&userMessages, &MessageService::messageAdded);
    net::Envelope incoming = post;
    incoming.header.insert(QStringLiteral("sender_login"), QStringLiteral("me"));
    userTransport.deliver(incoming);
    QTRY_VERIFY_WITH_TIMEOUT(added.count() >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(messageCount(workerB, chatId), 1, 5000);
    const auto storedMessage = fetchMessage(workerB, chatId, incoming.clientMsgId);
    QVERIFY(storedMessage.has_value());
    QCOMPARE(storedMessage->body, QStringLiteral("First post"));
    QCOMPARE(storedMessage->senderLogin, QStringLiteral("me"));
    QCOMPARE(fetchChat(workerB, chatId)->kind, QStringLiteral("channel"));
}

void TestChannelService::viewsAndInviteUseTheServer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    storage::StorageWorker worker;
    QVERIFY(worker.open(dir.filePath(QStringLiteral("db.sqlite")), dbKey()).isOk());

    ChannelStore store;
    FakeChannelApi api(&store, QStringLiteral("me"));
    ChannelService channels(&worker);
    channels.setApi(&api);
    channels.setSelfIdentity(QStringLiteral("u_me"), QStringLiteral("me"));

    ChannelDraft draft;
    draft.title = QStringLiteral("News");
    Channel created;
    bool done = false;
    channels.createChannel(draft, [&](Result<Channel> result) {
        created = result.isOk() ? result.value() : Channel{};
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done && created.isValid(), 5000);

    int views = -1;
    bool viewed = false;
    channels.recordView(created.channelId, QStringLiteral("post_1"), [&](Result<int> result) {
        views = result.isOk() ? result.value() : -1;
        viewed = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(viewed, 3000);
    QCOMPARE(views, 1);

    QString link;
    bool invited = false;
    channels.createInvite(created.channelId, [&](Result<QString> result) {
        link = result.isOk() ? result.value() : QString();
        invited = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(invited, 3000);
    QVERIFY(link.startsWith(QStringLiteral("mini://c/")));
    QVERIFY(link.contains(created.channelId));
}

QTEST_MAIN(TestChannelService)
#include "test_channel_service.moc"
