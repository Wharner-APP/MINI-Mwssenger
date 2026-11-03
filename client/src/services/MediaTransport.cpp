#include "services/MediaTransport.h"

#include "net/ApiClient.h"

namespace mini::services {

MediaTransport::MediaTransport(QObject *parent) : QObject(parent) {}
MediaTransport::~MediaTransport() = default;

ApiMediaTransport::ApiMediaTransport(net::ApiClient *api, QObject *parent)
    : MediaTransport(parent), m_api(api)
{
}

void ApiMediaTransport::initUpload(qint64 totalSize, qint64 chunkSize, InitCallback done)
{
    if (!m_api) {
        done(net::ApiResult<net::MediaUploadSession>::fail(
            net::ApiError::network(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->mediaInit(totalSize, chunkSize, std::move(done));
}

void ApiMediaTransport::putChunk(const QString &uploadId, int idx,
                                 const QByteArray &data, ChunkCallback done)
{
    if (!m_api) {
        done(net::ApiResult<net::MediaChunkResult>::fail(
            net::ApiError::network(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->mediaPutChunk(uploadId, idx, data, std::move(done));
}

void ApiMediaTransport::complete(const QString &uploadId, CompleteCallback done)
{
    if (!m_api) {
        done(net::ApiResult<net::MediaCompleteResult>::fail(
            net::ApiError::network(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->mediaComplete(uploadId, std::move(done));
}

void ApiMediaTransport::getChunk(const QString &uploadId, int idx, BytesCallback done)
{
    if (!m_api) {
        done(net::ApiResult<QByteArray>::fail(
            net::ApiError::network(QStringLiteral("api_unavailable"))));
        return;
    }
    m_api->mediaGetChunk(uploadId, idx, std::move(done));
}

} // namespace mini::services
