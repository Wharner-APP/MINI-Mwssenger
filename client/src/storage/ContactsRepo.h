#pragma once

#include "core/Result.h"
#include "domain/Contact.h"

#include <QString>
#include <QVector>

#include <optional>

namespace mini::storage {

class Database;

class ContactsRepo
{
public:
    explicit ContactsRepo(Database &db);

    Result<bool> upsert(const Contact &contact);
    Result<std::optional<Contact>> getByLogin(const QString &login) const;
    Result<QVector<Contact>> list() const;

private:
    Database &m_db;
};

} // namespace mini::storage
