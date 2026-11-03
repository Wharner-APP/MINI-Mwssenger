#include "storage/ContactsRepo.h"

#include "storage/Database.h"
#include "storage/Sql.h"

namespace mini::storage {
namespace {

constexpr auto kColumns = "login, display_name, safety_verified, updated_at";

Contact readContact(const detail::Statement &statement)
{
    Contact contact;
    contact.login = statement.text(0);
    contact.displayName = statement.text(1);
    contact.safetyVerified = statement.boolean(2);
    contact.updatedAt = statement.int64(3);
    return contact;
}

} // namespace

ContactsRepo::ContactsRepo(Database &db) : m_db(db)
{
}

Result<bool> ContactsRepo::upsert(const Contact &contact)
{
    if (contact.login.isEmpty()) {
        return Result<bool>::error(QStringLiteral("login is required"));
    }
    detail::Statement statement(
        m_db.handle(),
        R"sql(INSERT INTO contacts(login, display_name, safety_verified, updated_at)
              VALUES(?, ?, ?, ?)
              ON CONFLICT(login) DO UPDATE SET
                  display_name = excluded.display_name,
                  safety_verified = excluded.safety_verified,
                  updated_at = excluded.updated_at)sql");
    if (!statement.isOk()) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "contacts.upsert"));
    }
    statement.bindText(1, contact.login);
    statement.bindText(2, contact.displayName);
    statement.bindBool(3, contact.safetyVerified);
    statement.bindInt64(4, contact.updatedAt);
    if (statement.step() != SQLITE_DONE) {
        return Result<bool>::error(detail::sqlError(m_db.handle(), "contacts.upsert"));
    }
    return Result<bool>::ok(true);
}

Result<std::optional<Contact>> ContactsRepo::getByLogin(const QString &login) const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM contacts WHERE login = ?")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<std::optional<Contact>>::error(
            detail::sqlError(m_db.handle(), "contacts.getByLogin"));
    }
    statement.bindText(1, login);
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return Result<std::optional<Contact>>::ok(readContact(statement));
    }
    if (rc != SQLITE_DONE) {
        return Result<std::optional<Contact>>::error(
            detail::sqlError(m_db.handle(), "contacts.getByLogin"));
    }
    return Result<std::optional<Contact>>::ok(std::nullopt);
}

Result<QVector<Contact>> ContactsRepo::list() const
{
    detail::Statement statement(
        m_db.handle(),
        QStringLiteral("SELECT %1 FROM contacts ORDER BY login ASC")
            .arg(QString::fromLatin1(kColumns))
            .toUtf8()
            .constData());
    if (!statement.isOk()) {
        return Result<QVector<Contact>>::error(detail::sqlError(m_db.handle(), "contacts.list"));
    }

    QVector<Contact> contacts;
    int rc = statement.step();
    while (rc == SQLITE_ROW) {
        contacts.push_back(readContact(statement));
        rc = statement.step();
    }
    if (rc != SQLITE_DONE) {
        return Result<QVector<Contact>>::error(detail::sqlError(m_db.handle(), "contacts.list"));
    }
    return Result<QVector<Contact>>::ok(contacts);
}

} // namespace mini::storage
