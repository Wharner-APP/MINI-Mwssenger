#include "domain/Media.h"
#include <QCoreApplication>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace mini {

namespace {

constexpr int kWireVersion = 1;
constexpr int kMaxAlbumItems = 10;

QString base64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray fromBase64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

QJsonObject wireObject(const MediaWire &wire)
{
    QJsonObject media;
    media.insert(QStringLiteral("kind"), mediaKindToString(wire.kind));
    media.insert(QStringLiteral("file_id"), wire.fileId);
    media.insert(QStringLiteral("file_key"), base64(wire.fileKey));
    media.insert(QStringLiteral("nonce_base"), base64(wire.nonceBase));
    media.insert(QStringLiteral("size"), static_cast<double>(wire.size));
    media.insert(QStringLiteral("chunk_size"), static_cast<double>(wire.chunkSize));
    media.insert(QStringLiteral("file_hash"), wire.fileHash);
    media.insert(QStringLiteral("mime"), wire.mime);
    media.insert(QStringLiteral("name"), wire.name);
    media.insert(QStringLiteral("width"), wire.width);
    media.insert(QStringLiteral("height"), wire.height);
    media.insert(QStringLiteral("duration_ms"), static_cast<double>(wire.durationMs));
    media.insert(QStringLiteral("thumb"), base64(wire.thumb));
    return media;
}

bool wireFromObject(const QJsonObject &media, MediaWire *wire)
{
    if (!wire || media.isEmpty()) {
        return false;
    }
    wire->kind = mediaKindFromString(media.value(QStringLiteral("kind")).toString());
    if (wire->kind == MediaKind::Text) {
        wire->kind = MediaKind::File;
    }
    wire->fileId = media.value(QStringLiteral("file_id")).toString();
    wire->fileKey = fromBase64(media.value(QStringLiteral("file_key")));
    wire->nonceBase = fromBase64(media.value(QStringLiteral("nonce_base")));
    wire->size = static_cast<qint64>(media.value(QStringLiteral("size")).toDouble());
    wire->chunkSize =
        static_cast<qint64>(media.value(QStringLiteral("chunk_size")).toDouble());
    wire->fileHash = media.value(QStringLiteral("file_hash")).toString();
    wire->mime = media.value(QStringLiteral("mime")).toString();
    wire->name = media.value(QStringLiteral("name")).toString();
    wire->width = media.value(QStringLiteral("width")).toInt();
    wire->height = media.value(QStringLiteral("height")).toInt();
    wire->durationMs =
        static_cast<qint64>(media.value(QStringLiteral("duration_ms")).toDouble());
    wire->thumb = fromBase64(media.value(QStringLiteral("thumb")));
    return !wire->fileId.isEmpty() && wire->fileKey.size() == 32
        && wire->nonceBase.size() == 16 && wire->size > 0 && wire->chunkSize > 0;
}

bool isVersionedObject(const QByteArray &body, QJsonObject *root)
{
    if (!body.startsWith('{')) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("v")).toInt() != kWireVersion) {
        return false;
    }
    if (root) {
        *root = object;
    }
    return true;
}

} // namespace

QString mediaKindToString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Text:
        return QStringLiteral("text");
    case MediaKind::Photo:
        return QStringLiteral("photo");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::File:
        return QStringLiteral("file");
    case MediaKind::Sticker:
        return QStringLiteral("sticker");
    case MediaKind::Gif:
        return QStringLiteral("gif");
    case MediaKind::Album:
        return QStringLiteral("album");
    }
    return QStringLiteral("file");
}

MediaKind mediaKindFromString(const QString &value)
{
    if (value == QLatin1String("photo")) {
        return MediaKind::Photo;
    }
    if (value == QLatin1String("video")) {
        return MediaKind::Video;
    }
    if (value == QLatin1String("file")) {
        return MediaKind::File;
    }
    if (value == QLatin1String("sticker")) {
        return MediaKind::Sticker;
    }
    if (value == QLatin1String("gif")) {
        return MediaKind::Gif;
    }
    if (value == QLatin1String("album")) {
        return MediaKind::Album;
    }
    return MediaKind::Text;
}

bool mediaKindHasFile(MediaKind kind)
{
    return kind == MediaKind::Photo || kind == MediaKind::Video
        || kind == MediaKind::File || kind == MediaKind::Album;
}

QString mediaPreviewText(MediaKind kind, const QString &name, const QString &caption)
{
    if (!caption.trimmed().isEmpty()) {
        return caption;
    }
    switch (kind) {
    case MediaKind::Photo:
        return QCoreApplication::translate("Media", "Photo");
    case MediaKind::Video:
        return QCoreApplication::translate("Media", "Video");
    case MediaKind::File:
        return name.isEmpty() ? QCoreApplication::translate("Media", "File")
                              : QCoreApplication::translate("Media", "File: %1").arg(name);
    case MediaKind::Sticker:
        return QCoreApplication::translate("Media", "Sticker");
    case MediaKind::Gif:
        return QCoreApplication::translate("Media", "GIF");
    case MediaKind::Album:
        return QCoreApplication::translate("Media", "Album");
    case MediaKind::Text:
        break;
    }
    return {};
}

QJsonObject mediaWireToJson(const MediaWire &wire)
{
    QJsonObject root;
    root.insert(QStringLiteral("v"), kWireVersion);
    root.insert(QStringLiteral("media"), wireObject(wire));
    return root;
}

bool mediaWireFromJson(const QJsonObject &object, MediaWire *wire)
{
    if (!wire || object.value(QStringLiteral("v")).toInt() != kWireVersion) {
        return false;
    }
    return wireFromObject(object.value(QStringLiteral("media")).toObject(), wire);
}

QByteArray encodeStickerBody(const StickerRef &sticker)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("pack"), sticker.packId);
    payload.insert(QStringLiteral("id"), sticker.stickerId);
    payload.insert(QStringLiteral("title"), sticker.title);

    QJsonObject root;
    root.insert(QStringLiteral("v"), kWireVersion);
    root.insert(QStringLiteral("sticker"), payload);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool decodeStickerBody(const QByteArray &body, StickerRef *sticker)
{
    QJsonObject root;
    if (!sticker || !isVersionedObject(body, &root)) {
        return false;
    }
    const QJsonObject payload = root.value(QStringLiteral("sticker")).toObject();
    if (payload.isEmpty()) {
        return false;
    }
    sticker->packId = payload.value(QStringLiteral("pack")).toString();
    sticker->stickerId = payload.value(QStringLiteral("id")).toString();
    sticker->title = payload.value(QStringLiteral("title")).toString();
    return !sticker->packId.isEmpty() && !sticker->stickerId.isEmpty();
}

QByteArray encodeGifBody(const GifRef &gif)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), gif.gifId);
    payload.insert(QStringLiteral("title"), gif.title);

    QJsonObject root;
    root.insert(QStringLiteral("v"), kWireVersion);
    root.insert(QStringLiteral("gif"), payload);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool decodeGifBody(const QByteArray &body, GifRef *gif)
{
    QJsonObject root;
    if (!gif || !isVersionedObject(body, &root)) {
        return false;
    }
    const QJsonObject payload = root.value(QStringLiteral("gif")).toObject();
    if (payload.isEmpty()) {
        return false;
    }
    gif->gifId = payload.value(QStringLiteral("id")).toString();
    gif->title = payload.value(QStringLiteral("title")).toString();
    return !gif->gifId.isEmpty();
}

QByteArray encodeAlbumBody(const AlbumBody &album)
{
    QJsonObject root;
    root.insert(QStringLiteral("v"), kWireVersion);
    root.insert(QStringLiteral("caption"), album.caption);
    QJsonArray items;
    for (const MediaWire &wire : album.items) {
        items.append(wireObject(wire));
    }
    root.insert(QStringLiteral("items"), items);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool decodeAlbumBody(const QByteArray &body, AlbumBody *album)
{
    QJsonObject root;
    if (!album || !isVersionedObject(body, &root)) {
        return false;
    }
    const QJsonValue itemsValue = root.value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        return false;
    }
    const QJsonArray items = itemsValue.toArray();
    if (items.isEmpty() || items.size() > kMaxAlbumItems) {
        return false;
    }
    AlbumBody decoded;
    decoded.caption = root.value(QStringLiteral("caption")).toString();
    for (const QJsonValue &value : items) {
        MediaWire wire;
        if (!wireFromObject(value.toObject(), &wire)) {
            return false;
        }
        decoded.items.append(wire);
    }
    *album = decoded;
    return true;
}

QByteArray encodeMediaBody(MediaKind kind, const QString &caption, const MediaWire &wire)
{
    QJsonObject root = mediaWireToJson(wire);
    root.insert(QStringLiteral("kind"), mediaKindToString(kind));
    root.insert(QStringLiteral("caption"), caption);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool decodeMediaBody(const QByteArray &body, MediaKind *kind, QString *caption,
                     MediaWire *wire)
{
    if (!looksLikeMediaBody(body)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (!mediaWireFromJson(root, wire)) {
        return false;
    }
    if (kind) {
        *kind = mediaKindFromString(root.value(QStringLiteral("kind")).toString());
    }
    if (caption) {
        *caption = root.value(QStringLiteral("caption")).toString();
    }
    return true;
}

bool looksLikeMediaBody(const QByteArray &body)
{
    if (!body.startsWith('{')) {
        return false;
    }
    return body.contains("\"media\"") && body.contains("\"file_id\"");
}

RichBody decodeRichBody(const QByteArray &body)
{
    RichBody decoded;
    PollBody poll;
    if (decodePollBody(body, &poll)) {
        // §38.3: the poll body is JSON too, so it is checked before the media
        // decoders; the question doubles as the chat-list preview.
        decoded.hasPoll = true;
        decoded.poll = poll;
        decoded.kind = MediaKind::Text;
        decoded.caption = poll.question;
        return decoded;
    }
    AlbumBody album;
    if (decodeAlbumBody(body, &album)) {
        decoded.kind = MediaKind::Album;
        decoded.caption = album.caption;
        decoded.items = album.items;
        return decoded;
    }
    if (decodeStickerBody(body, &decoded.sticker)) {
        decoded.kind = MediaKind::Sticker;
        decoded.caption = decoded.sticker.title;
        return decoded;
    }
    if (decodeGifBody(body, &decoded.gif)) {
        decoded.kind = MediaKind::Gif;
        decoded.caption = decoded.gif.title;
        return decoded;
    }
    MediaKind media = MediaKind::Text;
    QString caption;
    MediaWire wire;
    if (decodeMediaBody(body, &media, &caption, &wire)) {
        decoded.kind = media;
        decoded.caption = caption;
        decoded.items.append(wire);
        return decoded;
    }
    decoded.kind = MediaKind::Text;
    decoded.caption = QString::fromUtf8(body);
    return decoded;
}

QString joinMediaIds(const QStringList &ids)
{
    return ids.join(QLatin1Char(','));
}

QStringList splitMediaIds(const QString &joined)
{
    return joined.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

} // namespace mini
