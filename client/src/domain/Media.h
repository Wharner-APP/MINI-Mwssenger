#pragma once

#include "domain/Poll.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mini {

// §11.5 / §40: the message kinds that carry a file or a built-in asset. Text
// stays the default so rows written before the media migration keep working.
enum class MediaKind {
    Text,
    Photo,
    Video,
    File,
    Sticker,
    Gif,
    Album,
};

[[nodiscard]] QString mediaKindToString(MediaKind kind);
[[nodiscard]] MediaKind mediaKindFromString(const QString &value);
// Label for the chat-list preview (§40.3-40.5), never the ciphertext.
[[nodiscard]] QString mediaPreviewText(MediaKind kind, const QString &name,
                                       const QString &caption);
// True for the kinds that carry an uploaded file (one or many).
[[nodiscard]] bool mediaKindHasFile(MediaKind kind);

// One uploaded file. fileKey/nonceBase are stored sealed by MediaRepo: the
// plaintext key never reaches the disk (§29.11).
struct MediaRecord
{
    QString mediaId;      // server upload id (up_...)
    QString chatId;
    MediaKind kind = MediaKind::File;
    QString mime;
    QString name;
    qint64 size = 0;      // plaintext bytes
    qint64 chunkSize = 0; // plaintext bytes per chunk; ciphertext chunk is +16
    QString fileHash;     // BLAKE2b-256 of the plaintext, hex
    QByteArray fileKey;   // 32 bytes, XChaCha20-Poly1305
    QByteArray nonceBase; // 16 bytes; nonce = base || uint64_be(chunk)
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;
    QString state; // prepared, uploading, uploaded, downloading, ready, failed
    int uploadedChunks = 0;
    int downloadedChunks = 0;
    QString localPath;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QByteArray thumb; // 320 px preview bytes (kept in media_meta, not here)
};

// The descriptor that travels inside the E2E envelope (§29.11 item 5).
struct MediaWire
{
    MediaKind kind = MediaKind::File; // per-item kind (an album may mix photo/video)
    QString fileId;
    QByteArray fileKey;
    QByteArray nonceBase;
    qint64 size = 0;
    qint64 chunkSize = 0;
    QString fileHash;
    QString mime;
    QString name;
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;
    QByteArray thumb; // small 320 px preview, base64 in JSON (§40.11)
};

[[nodiscard]] QJsonObject mediaWireToJson(const MediaWire &wire);
[[nodiscard]] bool mediaWireFromJson(const QJsonObject &object, MediaWire *wire);

// Built-in assets (§40.8, §40.9). The wire body is a small JSON document; the
// recipient resolves the id against its own bundled catalog, so no upload is
// needed and the message stays inside the normal E2E text pipeline.
struct StickerRef
{
    QString packId;
    QString stickerId;
    QString title;
};

[[nodiscard]] QByteArray encodeStickerBody(const StickerRef &sticker);
[[nodiscard]] bool decodeStickerBody(const QByteArray &body, StickerRef *sticker);

struct GifRef
{
    QString gifId;
    QString title;
};

[[nodiscard]] QByteArray encodeGifBody(const GifRef &gif);
[[nodiscard]] bool decodeGifBody(const QByteArray &body, GifRef *gif);

// §40.10: one message carries up to ten media descriptors.
struct AlbumBody
{
    QString caption;
    QVector<MediaWire> items;
};

[[nodiscard]] QByteArray encodeAlbumBody(const AlbumBody &album);
[[nodiscard]] bool decodeAlbumBody(const QByteArray &body, AlbumBody *album);

// Media messages reuse the text body slot: plaintext is a small JSON envelope,
// so the server still sees only ciphertext and old clients simply show the
// caption. Text messages are never wrapped.
[[nodiscard]] QByteArray encodeMediaBody(MediaKind kind, const QString &caption,
                                         const MediaWire &wire);
[[nodiscard]] bool decodeMediaBody(const QByteArray &body, MediaKind *kind,
                                   QString *caption, MediaWire *wire);
[[nodiscard]] bool looksLikeMediaBody(const QByteArray &body);

// One decoder for everything that can arrive inside a text envelope.
struct RichBody
{
    MediaKind kind = MediaKind::Text;
    QString caption;           // text body or media/album caption
    QVector<MediaWire> items;  // photo/video/file: one item; album: 1..10
    StickerRef sticker;
    GifRef gif;
    // §38.3: a poll travels as a JSON body inside the normal E2E envelope;
    // `kind` stays Text so old clients still show the question.
    bool hasPoll = false;
    PollBody poll;

    [[nodiscard]] bool hasUploadedMedia() const { return !items.isEmpty(); }
};

[[nodiscard]] RichBody decodeRichBody(const QByteArray &body);

// Album rows keep the comma-joined upload ids in messages.media_id.
[[nodiscard]] QString joinMediaIds(const QStringList &ids);
[[nodiscard]] QStringList splitMediaIds(const QString &joined);

} // namespace mini
