#include "net/ApiError.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace mini::net {

namespace {

QString fallbackCode(int status)
{
    return QStringLiteral("http_") + QString::number(status);
}

} // namespace

bool ApiError::isRetryable() const
{
    switch (kind) {
    case ApiErrorKind::Network:
    case ApiErrorKind::Timeout:
    case ApiErrorKind::RateLimited:
        return true;
    case ApiErrorKind::Http:
        return httpStatus >= 500;
    case ApiErrorKind::None:
    case ApiErrorKind::Tls:
    case ApiErrorKind::Unauthorized:
    case ApiErrorKind::Protocol:
    case ApiErrorKind::Cancelled:
        return false;
    }
    return false;
}

ApiError ApiError::network(const QString &detail)
{
    ApiError error;
    error.kind = ApiErrorKind::Network;
    error.code = QStringLiteral("network_error");
    error.message = detail;
    return error;
}

ApiError ApiError::timeout()
{
    ApiError error;
    error.kind = ApiErrorKind::Timeout;
    error.code = QStringLiteral("timeout");
    return error;
}

ApiError ApiError::tls(const QString &detail)
{
    ApiError error;
    error.kind = ApiErrorKind::Tls;
    error.code = QStringLiteral("tls_error");
    error.message = detail;
    return error;
}

ApiError ApiError::protocol(const QString &detail)
{
    ApiError error;
    error.kind = ApiErrorKind::Protocol;
    error.code = QStringLiteral("protocol_error");
    error.message = detail;
    return error;
}

ApiError ApiError::cancelled()
{
    ApiError error;
    error.kind = ApiErrorKind::Cancelled;
    error.code = QStringLiteral("cancelled");
    return error;
}

ApiError ApiError::fromStatus(int httpStatus, const QByteArray &body,
                              const QByteArray &retryAfterHeader)
{
    ApiError error;
    error.httpStatus = httpStatus;

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonObject serverError = root.value(QStringLiteral("error")).toObject();
    error.code = serverError.value(QStringLiteral("code")).toString();
    error.message = serverError.value(QStringLiteral("message")).toString();
    error.requestId = serverError.value(QStringLiteral("request_id")).toString();
    error.retryAfterMs =
        static_cast<qint64>(serverError.value(QStringLiteral("retry_after_ms")).toDouble(0));

    if (error.retryAfterMs == 0 && !retryAfterHeader.isEmpty()) {
        bool ok = false;
        const qint64 seconds = retryAfterHeader.trimmed().toLongLong(&ok);
        if (ok && seconds > 0) {
            error.retryAfterMs = seconds * 1000;
        }
    }

    if (httpStatus == 401) {
        error.kind = ApiErrorKind::Unauthorized;
    } else if (httpStatus == 429 || httpStatus == 423) {
        error.kind = ApiErrorKind::RateLimited;
        if (error.retryAfterMs == 0) {
            error.retryAfterMs = 1000;
        }
    } else {
        error.kind = ApiErrorKind::Http;
    }

    if (error.code.isEmpty()) {
        error.code = fallbackCode(httpStatus);
    }
    return error;
}

} // namespace mini::net
