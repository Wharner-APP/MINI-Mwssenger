#include "services/PrivacyService.h"

namespace mini::services {

namespace {

PrivacySettings fromWire(const net::PrivacySettings &wire)
{
    PrivacySettings settings;
    settings.whoCanMessage = PrivacyService::ruleFromId(wire.whoCanMessage);
    settings.whoCanCall = PrivacyService::ruleFromId(wire.whoCanCall);
    settings.whoSeesLastSeen = PrivacyService::ruleFromId(wire.whoSeesLastSeen);
    settings.readReceipts = wire.readReceipts;
    return settings;
}

net::PrivacySettings toWire(const PrivacySettings &settings)
{
    net::PrivacySettings wire;
    wire.whoCanMessage = PrivacyService::ruleId(settings.whoCanMessage);
    wire.whoCanCall = PrivacyService::ruleId(settings.whoCanCall);
    wire.whoSeesLastSeen = PrivacyService::ruleId(settings.whoSeesLastSeen);
    wire.readReceipts = settings.readReceipts;
    return wire;
}

} // namespace

PrivacyService::PrivacyService(QObject *parent) : QObject(parent) {}

QString PrivacyService::ruleId(PrivacyRule rule)
{
    switch (rule) {
    case PrivacyRule::Contacts:
        return QStringLiteral("contacts");
    case PrivacyRule::Nobody:
        return QStringLiteral("nobody");
    case PrivacyRule::Everyone:
    default:
        return QStringLiteral("everyone");
    }
}

PrivacyRule PrivacyService::ruleFromId(const QString &id)
{
    const QString value = id.trimmed().toLower();
    if (value == QLatin1String("contacts")) {
        return PrivacyRule::Contacts;
    }
    if (value == QLatin1String("nobody")) {
        return PrivacyRule::Nobody;
    }
    return PrivacyRule::Everyone;
}

QString PrivacyService::ruleLabel(PrivacyRule rule)
{
    switch (rule) {
    case PrivacyRule::Contacts:
        return QStringLiteral("contacts");
    case PrivacyRule::Nobody:
        return QStringLiteral("nobody");
    case PrivacyRule::Everyone:
    default:
        return QStringLiteral("everyone");
    }
}

Result<PrivacySettings> PrivacyService::validate(const PrivacySettings &settings)
{
    PrivacySettings copy = settings;
    // The enum has only the three wire values; a corrupted cast is normalized
    // to "everyone" instead of being sent as garbage.
    copy.whoCanMessage = ruleFromId(ruleId(settings.whoCanMessage));
    copy.whoCanCall = ruleFromId(ruleId(settings.whoCanCall));
    copy.whoSeesLastSeen = ruleFromId(ruleId(settings.whoSeesLastSeen));
    return Result<PrivacySettings>::ok(copy);
}

QString PrivacyService::apiError(const net::ApiError &error)
{
    if (!error.code.isEmpty()) {
        return error.code;
    }
    switch (error.kind) {
    case net::ApiErrorKind::Network:
        return QStringLiteral("network_unavailable");
    case net::ApiErrorKind::Timeout:
        return QStringLiteral("timeout");
    case net::ApiErrorKind::Unauthorized:
        return QStringLiteral("unauthorized");
    default:
        return QStringLiteral("privacy_request_failed");
    }
}

void PrivacyService::loadSettings(Done<PrivacySettings> done)
{
    if (!m_api) {
        done(Result<PrivacySettings>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->getSettings([done = std::move(done)](
                           net::ApiResult<net::PrivacySettings> result) {
        if (result.isError()) {
            done(Result<PrivacySettings>::error(apiError(result.error())));
            return;
        }
        done(Result<PrivacySettings>::ok(fromWire(result.value())));
    });
}

void PrivacyService::saveSettings(const PrivacySettings &settings,
                                  Done<PrivacySettings> done)
{
    const Result<PrivacySettings> valid = validate(settings);
    if (valid.isError()) {
        done(Result<PrivacySettings>::error(valid.error()));
        return;
    }
    if (!m_api) {
        done(Result<PrivacySettings>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->updateSettings(
        toWire(valid.value()),
        [this, done = std::move(done)](net::ApiResult<net::PrivacySettings> result) {
            if (result.isError()) {
                done(Result<PrivacySettings>::error(apiError(result.error())));
                return;
            }
            const PrivacySettings saved = fromWire(result.value());
            emit settingsChanged(saved);
            done(Result<PrivacySettings>::ok(saved));
        });
}

void PrivacyService::listBlocks(Done<QVector<BlockedUser>> done)
{
    if (!m_api) {
        done(Result<QVector<BlockedUser>>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->listBlocks(
        [done = std::move(done)](net::ApiResult<QList<net::BlockedUserInfo>> result) {
            if (result.isError()) {
                done(Result<QVector<BlockedUser>>::error(apiError(result.error())));
                return;
            }
            QVector<BlockedUser> users;
            users.reserve(result.value().size());
            for (const net::BlockedUserInfo &info : result.value()) {
                BlockedUser user;
                user.userId = info.userId;
                user.login = info.login;
                user.displayName = info.displayName;
                user.createdAt = info.createdAt;
                users.append(user);
            }
            done(Result<QVector<BlockedUser>>::ok(users));
        });
}

void PrivacyService::block(const QString &userOrLogin, Done<bool> done)
{
    QString value = userOrLogin.trimmed();
    if (value.startsWith(QLatin1Char('@'))) {
        value.remove(0, 1);
    }
    if (value.isEmpty() || value.size() > 64) {
        done(Result<bool>::error(QStringLiteral("block.user_invalid")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->blockUser(value, [this, done = std::move(done)](net::ApiResult<net::Empty> result) {
        if (result.isError()) {
            done(Result<bool>::error(apiError(result.error())));
            return;
        }
        emit blocksChanged();
        done(Result<bool>::ok(true));
    });
}

void PrivacyService::unblock(const QString &userId, Done<bool> done)
{
    if (userId.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("block.user_required")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->unblockUser(userId.trimmed(),
                       [this, done = std::move(done)](net::ApiResult<net::Empty> result) {
                           if (result.isError()) {
                               done(Result<bool>::error(apiError(result.error())));
                               return;
                           }
                           emit blocksChanged();
                           done(Result<bool>::ok(true));
                       });
}

void PrivacyService::listDevices(Done<QVector<DeviceEntry>> done)
{
    if (!m_api) {
        done(Result<QVector<DeviceEntry>>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->listDevices(
        [done = std::move(done)](net::ApiResult<QList<net::DeviceRecord>> result) {
            if (result.isError()) {
                done(Result<QVector<DeviceEntry>>::error(apiError(result.error())));
                return;
            }
            QVector<DeviceEntry> devices;
            devices.reserve(result.value().size());
            for (const net::DeviceRecord &info : result.value()) {
                DeviceEntry entry;
                entry.deviceId = info.deviceId;
                entry.name = info.name;
                entry.platform = info.platform;
                entry.ip = info.ip;
                entry.createdAt = info.createdAt;
                entry.lastSeenAt = info.lastSeenAt;
                entry.current = info.current;
                devices.append(entry);
            }
            done(Result<QVector<DeviceEntry>>::ok(devices));
        });
}

void PrivacyService::revokeDevice(const QString &deviceId, Done<bool> done)
{
    if (deviceId.trimmed().isEmpty()) {
        done(Result<bool>::error(QStringLiteral("device.id_required")));
        return;
    }
    if (!m_api) {
        done(Result<bool>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->revokeDevice(deviceId.trimmed(),
                        [this, done = std::move(done)](net::ApiResult<net::Empty> result) {
                            if (result.isError()) {
                                done(Result<bool>::error(apiError(result.error())));
                                return;
                            }
                            emit devicesChanged();
                            done(Result<bool>::ok(true));
                        });
}

void PrivacyService::revokeAllOtherSessions(Done<int> done)
{
    if (!m_api) {
        done(Result<int>::error(QStringLiteral("privacy_unavailable")));
        return;
    }
    m_api->revokeAllDevices([this, done = std::move(done)](
                                net::ApiResult<net::RevokeAllResult> result) {
        if (result.isError()) {
            done(Result<int>::error(apiError(result.error())));
            return;
        }
        emit devicesChanged();
        done(Result<int>::ok(result.value().revokedDevices));
    });
}

} // namespace mini::services
