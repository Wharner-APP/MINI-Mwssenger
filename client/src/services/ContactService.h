#pragma once

#include "core/Result.h"
#include "domain/Contact.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mini::storage {
class StorageWorker;
}

namespace mini::services {

struct PeerProfile
{
    QString login;
    QString displayName;
    QString userId;
    bool found = false;
    bool online = false;
    qint64 lastActiveAt = 0;
};

// The signed-in account as the profile screen shows it.
struct SelfProfile
{
    QString userId;
    QString login;
    QString displayName;
    QString bio;
    bool online = false;
    qint64 lastActiveAt = 0;
};

// Resolves an exact login to a profile and keeps the local contact cache.
// The directory call is injected: the users endpoint is added in T09, so this
// service must not pretend ApiClient already offers it.
class ContactService : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Done = std::function<void(Result<T>)>;

    using Lookup = std::function<void(const QString &login,
                                      std::function<void(Result<PeerProfile>)> done)>;
    using SelfFetcher = std::function<void(std::function<void(Result<SelfProfile>)>)>;
    using SelfUpdater =
        std::function<void(const QString &displayName, const QString &bio,
                           std::function<void(Result<SelfProfile>)> done)>;

    explicit ContactService(storage::StorageWorker *storage, QObject *parent = nullptr);

    void setLookup(Lookup lookup) { m_lookup = std::move(lookup); }
    [[nodiscard]] bool hasLookup() const { return static_cast<bool>(m_lookup); }

    // Own profile: the directory seam is injected for the same reason as
    // Lookup — the service never assumes more of ApiClient than it has.
    void setSelfSource(SelfFetcher fetch, SelfUpdater update);
    [[nodiscard]] bool hasSelfSource() const
    {
        return static_cast<bool>(m_selfFetch) && static_cast<bool>(m_selfUpdate);
    }
    void fetchSelf(Done<SelfProfile> done);
    // Validates the name (1-64 Unicode) and the bio (up to 200) first.
    void updateSelf(const QString &displayName, const QString &bio, Done<SelfProfile> done);

    // Pure helpers shared by the profile screen and its tests.
    [[nodiscard]] static QString initialsFor(const QString &displayName,
                                             const QString &login = QString());
    [[nodiscard]] static Result<QString> validateBio(const QString &bio);
    // Compact age: "now", "5m", "2h", "3d".
    [[nodiscard]] static QString relativeShort(qint64 deltaMs);

    // Validates the format locally, then asks the directory. An unknown login
    // yields error "user_not_found"; an invalid one "login.invalid_format".
    void resolve(const QString &login, Done<PeerProfile> done);

    void listContacts(Done<QVector<Contact>> done);
    void saveContact(const Contact &contact, Done<bool> done);

signals:
    void contactSaved(const mini::Contact &contact);

private:
    storage::StorageWorker *m_storage = nullptr;
    Lookup m_lookup;
    SelfFetcher m_selfFetch;
    SelfUpdater m_selfUpdate;
};

} // namespace mini::services
