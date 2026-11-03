#pragma once

#include <QByteArray>
#include <QString>

#include <sqlite3.h>

namespace mini::storage::detail {

// Thin RAII wrapper around a prepared statement. Repositories keep all SQL
// text local to themselves; nothing here is exposed outside storage/.
class Statement
{
public:
    Statement(sqlite3 *db, const char *sql)
    {
        m_rc = sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr);
    }

    ~Statement()
    {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    [[nodiscard]] bool isOk() const { return m_rc == SQLITE_OK; }
    [[nodiscard]] int lastError() const { return m_rc; }
    [[nodiscard]] sqlite3_stmt *get() const { return m_stmt; }

    void bindText(int index, const QString &value)
    {
        const QByteArray utf8 = value.toUtf8();
        sqlite3_bind_text(m_stmt, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
    }

    void bindTextOrNull(int index, const QString &value)
    {
        if (value.isEmpty()) {
            sqlite3_bind_null(m_stmt, index);
        } else {
            bindText(index, value);
        }
    }

    void bindBlob(int index, const QByteArray &value)
    {
        sqlite3_bind_blob(m_stmt, index, value.constData(), value.size(), SQLITE_TRANSIENT);
    }

    void bindInt64(int index, qint64 value) { sqlite3_bind_int64(m_stmt, index, value); }
    void bindInt(int index, int value) { sqlite3_bind_int(m_stmt, index, value); }
    void bindBool(int index, bool value) { sqlite3_bind_int(m_stmt, index, value ? 1 : 0); }
    void bindNull(int index) { sqlite3_bind_null(m_stmt, index); }

    [[nodiscard]] int step() { return sqlite3_step(m_stmt); }

    [[nodiscard]] QString text(int column) const
    {
        const auto *data = sqlite3_column_text(m_stmt, column);
        if (!data) {
            return QString();
        }
        return QString::fromUtf8(reinterpret_cast<const char *>(data),
                                 sqlite3_column_bytes(m_stmt, column));
    }

    [[nodiscard]] QByteArray blob(int column) const
    {
        const auto *data = static_cast<const char *>(sqlite3_column_blob(m_stmt, column));
        const int size = sqlite3_column_bytes(m_stmt, column);
        if (!data || size <= 0) {
            return QByteArray();
        }
        return QByteArray(data, size);
    }

    [[nodiscard]] qint64 int64(int column) const { return sqlite3_column_int64(m_stmt, column); }
    [[nodiscard]] int integer(int column) const { return sqlite3_column_int(m_stmt, column); }
    [[nodiscard]] bool boolean(int column) const { return sqlite3_column_int(m_stmt, column) != 0; }

private:
    sqlite3_stmt *m_stmt = nullptr;
    int m_rc = SQLITE_ERROR;
};

[[nodiscard]] inline QString sqlError(sqlite3 *db, const char *context)
{
    return QStringLiteral("%1: %2")
        .arg(QString::fromLatin1(context), QString::fromUtf8(sqlite3_errmsg(db)));
}

} // namespace mini::storage::detail
