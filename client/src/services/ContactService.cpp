#include "services/ContactService.h"

#include "core/Clock.h"
#include "services/AuthService.h"
#include "storage/ContactsRepo.h"
#include "storage/Database.h"
#include "storage/StorageWorker.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QRegularExpression>

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

} // namespace

ContactService::ContactService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void ContactService::resolve(const QString &login, Done<PeerProfile> done)
{
    const Result<QString> valid = AuthService::validateLogin(login);
    if (valid.isError()) {
        done(Result<PeerProfile>::error(valid.error()));
        return;
    }
    if (!m_lookup) {
        done(Result<PeerProfile>::error(QStringLiteral("directory_unavailable")));
        return;
    }

    const QString normalized = valid.value();
    m_lookup(normalized, [this, normalized, done = std::move(done)](
                             Result<PeerProfile> result) {
        if (result.isError()) {
            done(result);
            return;
        }
        if (!result.value().found) {
            done(Result<PeerProfile>::error(QStringLiteral("user_not_found")));
            return;
        }
        PeerProfile profile = result.value();
        if (profile.login.isEmpty()) {
            profile.login = normalized;
        }

        Contact contact;
        contact.login = profile.login;
        contact.displayName = profile.displayName;
        contact.updatedAt = Clock::nowMs();

        if (!m_storage) {
            done(Result<PeerProfile>::ok(profile));
            return;
        }
        auto future = m_storage->submit([contact](storage::Database &db) {
            return storage::ContactsRepo(db).upsert(contact).isOk();
        });
        watch<bool>(this, future, [this, profile, contact,
                                   done = std::move(done)](bool saved) {
            if (saved) {
                emit contactSaved(contact);
            }
            done(Result<PeerProfile>::ok(profile));
        });
    });
}

void ContactService::setSelfSource(SelfFetcher fetch, SelfUpdater update)
{
    m_selfFetch = std::move(fetch);
    m_selfUpdate = std::move(update);
}

void ContactService::fetchSelf(Done<SelfProfile> done)
{
    if (!m_selfFetch) {
        done(Result<SelfProfile>::error(QStringLiteral("profile_unavailable")));
        return;
    }
    m_selfFetch(std::move(done));
}

void ContactService::updateSelf(const QString &displayName, const QString &bio,
                                Done<SelfProfile> done)
{
    const Result<QString> name = AuthService::validateDisplayName(displayName);
    if (name.isError()) {
        done(Result<SelfProfile>::error(name.error()));
        return;
    }
    const Result<QString> about = validateBio(bio);
    if (about.isError()) {
        done(Result<SelfProfile>::error(about.error()));
        return;
    }
    if (!m_selfUpdate) {
        done(Result<SelfProfile>::error(QStringLiteral("profile_unavailable")));
        return;
    }
    m_selfUpdate(name.value(), about.value(), std::move(done));
}

QString ContactService::initialsFor(const QString &displayName, const QString &login)
{
    const QString source =
        displayName.trimmed().isEmpty() ? login.trimmed() : displayName.trimmed();
    if (source.isEmpty()) {
        return QStringLiteral("?");
    }

    const QStringList words =
        source.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString initials;
    int taken = 0;
    for (const QString &word : words) {
        if (word.isEmpty()) {
            continue;
        }
        QString first = word.left(1);
        if (first.at(0).isHighSurrogate() && word.size() > 1) {
            first = word.left(2);
        }
        initials.append(first);
        if (++taken == 2) {
            break;
        }
    }
    return initials.isEmpty() ? QStringLiteral("?") : initials.toUpper();
}

Result<QString> ContactService::validateBio(const QString &bio)
{
    const QString trimmed = bio.trimmed();
    for (const QChar character : trimmed) {
        const ushort code = character.unicode();
        if (code < 0x20 || (code >= 0x7f && code <= 0x9f)) {
            return Result<QString>::error(QStringLiteral("profile.bio_invalid"));
        }
    }
    if (trimmed.toUcs4().size() > 200) {
        return Result<QString>::error(QStringLiteral("profile.bio_too_long"));
    }
    return Result<QString>::ok(trimmed);
}

QString ContactService::relativeShort(qint64 deltaMs)
{
    const qint64 seconds = qMax<qint64>(0, deltaMs) / 1000;
    if (seconds < 60) {
        return QStringLiteral("now");
    }
    if (seconds < 3600) {
        return QStringLiteral("%1m").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return QStringLiteral("%1h").arg(seconds / 3600);
    }
    return QStringLiteral("%1d").arg(seconds / 86400);
}

void ContactService::listContacts(Done<QVector<Contact>> done)
{
    if (!m_storage) {
        done(Result<QVector<Contact>>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    auto future = m_storage->submit([](storage::Database &db) {
        return storage::ContactsRepo(db).list();
    });
    watch<Result<QVector<Contact>>>(this, future,
                                    [done = std::move(done)](Result<QVector<Contact>> result) {
                                        done(std::move(result));
                                    });
}

void ContactService::saveContact(const Contact &contact, Done<bool> done)
{
    if (!m_storage) {
        done(Result<bool>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    Contact stored = contact;
    if (stored.updatedAt == 0) {
        stored.updatedAt = Clock::nowMs();
    }
    auto future = m_storage->submit([stored](storage::Database &db) {
        return storage::ContactsRepo(db).upsert(stored).isOk();
    });
    watch<bool>(this, future, [this, stored, done = std::move(done)](bool saved) {
        if (saved) {
            emit contactSaved(stored);
        }
        done(Result<bool>::ok(saved));
    });
}

} // namespace mini::services
