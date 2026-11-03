#pragma once

#include <QByteArray>
#include <QString>

#include <utility>

namespace mini::net {

enum class ApiErrorKind {
    None,
    Network,
    Timeout,
    Tls,
    Unauthorized,
    RateLimited,
    Http,
    Protocol,
    Cancelled
};

// A typed API failure. The server sends {"error":{"code","message",...}};
// only the decoded fields leave the network layer, never raw JSON.
struct ApiError {
    ApiErrorKind kind = ApiErrorKind::None;
    int httpStatus = 0;
    QString code;
    QString message;
    qint64 retryAfterMs = 0;
    QString requestId;

    [[nodiscard]] bool isError() const { return kind != ApiErrorKind::None; }
    [[nodiscard]] bool isRetryable() const;

    [[nodiscard]] static ApiError network(const QString &detail);
    [[nodiscard]] static ApiError timeout();
    [[nodiscard]] static ApiError tls(const QString &detail);
    [[nodiscard]] static ApiError protocol(const QString &detail);
    [[nodiscard]] static ApiError cancelled();
    [[nodiscard]] static ApiError fromStatus(int httpStatus, const QByteArray &body,
                                             const QByteArray &retryAfterHeader = {});
};

template <typename T>
class ApiResult
{
public:
    static ApiResult ok(T value) { return ApiResult(std::move(value), ApiError()); }
    static ApiResult fail(ApiError error) { return ApiResult(T(), std::move(error)); }

    [[nodiscard]] bool isOk() const { return !m_error.isError(); }
    [[nodiscard]] bool isError() const { return m_error.isError(); }
    [[nodiscard]] const T &value() const { return m_value; }
    [[nodiscard]] T &value() { return m_value; }
    [[nodiscard]] const ApiError &error() const { return m_error; }

private:
    ApiResult(T value, ApiError error) : m_value(std::move(value)), m_error(std::move(error)) {}

    T m_value;
    ApiError m_error;
};

struct Empty {
};

} // namespace mini::net
