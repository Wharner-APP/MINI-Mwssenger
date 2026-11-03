#pragma once

#include <QString>

#include <utility>

namespace mini {

// Small success/error container used instead of exceptions on hot paths.
template <typename T>
class Result
{
public:
    static Result ok(T value) { return Result(std::move(value), QString()); }
    static Result error(QString message) { return Result(T(), std::move(message)); }

    [[nodiscard]] bool isOk() const { return m_error.isEmpty(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] const T &value() const { return m_value; }
    [[nodiscard]] T &value() { return m_value; }
    [[nodiscard]] const QString &error() const { return m_error; }

private:
    Result(T value, QString error) : m_value(std::move(value)), m_error(std::move(error)) {}

    T m_value;
    QString m_error;
};

} // namespace mini
