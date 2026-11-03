#pragma once

#include "net/ApiError.h"
#include "net/ApiTypes.h"

#include <QObject>
#include <QString>

#include <functional>

namespace mini::net {
class ApiClient;
}

namespace mini::services {

// Network seam of the media pipeline. Production uses ApiMediaTransport;
// tests implement the same four calls in memory, so MediaService can be
// exercised without a server.
class MediaTransport : public QObject
{
    Q_OBJECT

public:
    using InitCallback = std::function<void(const net::ApiResult<net::MediaUploadSession> &)>;
    using ChunkCallback = std::function<void(const net::ApiResult<net::MediaChunkResult> &)>;
    using CompleteCallback =
        std::function<void(const net::ApiResult<net::MediaCompleteResult> &)>;
    using BytesCallback = std::function<void(const net::ApiResult<QByteArray> &)>;

    explicit MediaTransport(QObject *parent = nullptr);
    ~MediaTransport() override;

    virtual void initUpload(qint64 totalSize, qint64 chunkSize,
                            InitCallback done) = 0;
    virtual void putChunk(const QString &uploadId, int idx, const QByteArray &data,
                          ChunkCallback done) = 0;
    virtual void complete(const QString &uploadId, CompleteCallback done) = 0;
    virtual void getChunk(const QString &uploadId, int idx, BytesCallback done) = 0;
};

class ApiMediaTransport : public MediaTransport
{
    Q_OBJECT

public:
    explicit ApiMediaTransport(net::ApiClient *api, QObject *parent = nullptr);

    void initUpload(qint64 totalSize, qint64 chunkSize, InitCallback done) override;
    void putChunk(const QString &uploadId, int idx, const QByteArray &data,
                  ChunkCallback done) override;
    void complete(const QString &uploadId, CompleteCallback done) override;
    void getChunk(const QString &uploadId, int idx, BytesCallback done) override;

private:
    net::ApiClient *m_api = nullptr;
};

} // namespace mini::services
