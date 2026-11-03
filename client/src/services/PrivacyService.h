#pragma once

#include "core/Result.h"
#include "services/PrivacyApi.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::services {

enum class PrivacyRule {
    Everyone,
    Contacts,
    Nobody
};

struct PrivacySettings
{
    PrivacyRule whoCanMessage = PrivacyRule::Everyone;
    PrivacyRule whoCanCall = PrivacyRule::Everyone;
    PrivacyRule whoSeesLastSeen = PrivacyRule::Everyone;
    bool readReceipts = true;
};

struct BlockedUser
{
    QString userId;
    QString login;
    QString displayName;
    qint64 createdAt = 0;
};

struct DeviceEntry
{
    QString deviceId;
    QString name;
    QString platform;
    QString ip;
    qint64 createdAt = 0;
    qint64 lastSeenAt = 0;
    bool current = false;
};

// Privacy switches (§11.12 #177-180), the blacklist (#181) and the session
// list (#182, §28.16). The server owns the rules; this service only validates
// and maps them, so the UI never sees raw JSON.
class PrivacyService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    explicit PrivacyService(QObject *parent = nullptr);

    void setApi(PrivacyApi *api) { m_api = api; }
    [[nodiscard]] bool hasApi() const { return m_api != nullptr; }

    [[nodiscard]] static QString ruleId(PrivacyRule rule);
    [[nodiscard]] static PrivacyRule ruleFromId(const QString &id);
    [[nodiscard]] static QString ruleLabel(PrivacyRule rule);
    [[nodiscard]] static Result<PrivacySettings> validate(const PrivacySettings &settings);

    void loadSettings(Done<PrivacySettings> done);
    void saveSettings(const PrivacySettings &settings, Done<PrivacySettings> done);

    void listBlocks(Done<QVector<BlockedUser>> done);
    // Accepts a user id or an exact login; an empty value is rejected locally.
    void block(const QString &userOrLogin, Done<bool> done);
    void unblock(const QString &userId, Done<bool> done);

    void listDevices(Done<QVector<DeviceEntry>> done);
    void revokeDevice(const QString &deviceId, Done<bool> done);
    // "End all other sessions": revokes every device except the current one.
    void revokeAllOtherSessions(Done<int> done);

signals:
    void settingsChanged(const mini::services::PrivacySettings &settings);
    void blocksChanged();
    void devicesChanged();

private:
    static QString apiError(const net::ApiError &error);

    PrivacyApi *m_api = nullptr;
};

} // namespace mini::services

Q_DECLARE_METATYPE(mini::services::PrivacySettings)
