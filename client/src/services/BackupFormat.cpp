#include "services/BackupFormat.h"

#include "crypto/Kdf.h"
#include "crypto/Sodium.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <sodium.h>

#include <cstring>

#ifdef MINI_HAVE_ZIP
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>
#endif

namespace mini::services::backup {
namespace {

constexpr int kNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr int kTagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr int kKeyBytes = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr int kHeaderBytes = 8 + 1;
constexpr qint64 kMaxArchiveBytes = 1024LL * 1024 * 1024; // 1 GiB

QByteArray magicBytes()
{
    return QByteArray(kMagic, static_cast<int>(sizeof(kMagic)));
}

QByteArray versionByte(int version)
{
    return QByteArray(1, static_cast<char>(version));
}

QByteArray aad()
{
    return magicBytes() + versionByte(kVersion);
}

} // namespace

QByteArray deriveExportKey(const QByteArray &kMac)
{
    return crypto::Kdf::hkdfSha256(kMac, {}, QByteArrayLiteral("export/v1"), kKeyBytes);
}

Result<QByteArray> sealArchive(const QByteArray &zip, const QByteArray &key)
{
    if (key.size() != kKeyBytes) {
        return Result<QByteArray>::error(QStringLiteral("backup.key_invalid"));
    }
    if (zip.size() > kMaxArchiveBytes) {
        return Result<QByteArray>::error(QStringLiteral("backup.too_large"));
    }
    if (!crypto::Sodium::init()) {
        return Result<QByteArray>::error(QStringLiteral("backup.crypto_unavailable"));
    }

    const QByteArray header = aad();
    QByteArray sealed(kHeaderBytes + kNonceBytes + zip.size() + kTagBytes, '\0');
    memcpy(sealed.data(), kMagic, sizeof(kMagic));
    sealed[8] = static_cast<char>(kVersion);
    auto *nonce = reinterpret_cast<unsigned char *>(sealed.data() + kHeaderBytes);
    randombytes_buf(nonce, kNonceBytes);

    unsigned long long cipherLength = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(sealed.data() + kHeaderBytes + kNonceBytes),
        &cipherLength, reinterpret_cast<const unsigned char *>(zip.constData()),
        static_cast<unsigned long long>(zip.size()),
        reinterpret_cast<const unsigned char *>(header.constData()),
        static_cast<unsigned long long>(header.size()), nullptr, nonce,
        reinterpret_cast<const unsigned char *>(key.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("backup.encrypt_failed"));
    }
    sealed.resize(kHeaderBytes + kNonceBytes + static_cast<int>(cipherLength));
    return Result<QByteArray>::ok(sealed);
}

Result<QByteArray> openArchive(const QByteArray &file, const QByteArray &key)
{
    if (file.size() < 8 || memcmp(file.constData(), kMagic, sizeof(kMagic)) != 0) {
        return Result<QByteArray>::error(QStringLiteral("backup.bad_magic"));
    }
    if (file.size() < kHeaderBytes) {
        return Result<QByteArray>::error(QStringLiteral("backup.truncated"));
    }
    if (static_cast<quint8>(file.at(8)) != kVersion) {
        return Result<QByteArray>::error(QStringLiteral("backup.unsupported_version"));
    }
    if (key.size() != kKeyBytes) {
        return Result<QByteArray>::error(QStringLiteral("backup.key_invalid"));
    }
    if (file.size() < kHeaderBytes + kNonceBytes + kTagBytes) {
        return Result<QByteArray>::error(QStringLiteral("backup.truncated"));
    }
    if (!crypto::Sodium::init()) {
        return Result<QByteArray>::error(QStringLiteral("backup.crypto_unavailable"));
    }

    const QByteArray header = aad();
    const auto *nonce =
        reinterpret_cast<const unsigned char *>(file.constData() + kHeaderBytes);
    const int cipherBytes =
        file.size() - kHeaderBytes - kNonceBytes;
    QByteArray plaintext(cipherBytes, '\0');
    unsigned long long plainLength = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plaintext.data()), &plainLength, nullptr,
        reinterpret_cast<const unsigned char *>(file.constData() + kHeaderBytes
                                                + kNonceBytes),
        static_cast<unsigned long long>(cipherBytes),
        reinterpret_cast<const unsigned char *>(header.constData()),
        static_cast<unsigned long long>(header.size()), nonce,
        reinterpret_cast<const unsigned char *>(key.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("backup.decrypt_failed"));
    }
    plaintext.resize(static_cast<int>(plainLength));
    return Result<QByteArray>::ok(plaintext);
}

Result<QByteArray>
buildZip(const QVector<std::pair<QString, QByteArray>> &entries)
{
#ifdef MINI_HAVE_ZIP
    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly)) {
        return Result<QByteArray>::error(QStringLiteral("backup.archive_failed"));
    }
    QZipWriter writer(&buffer);
    writer.setCompressionPolicy(QZipWriter::AutoCompress);
    for (const auto &entry : entries) {
        if (entry.first.isEmpty()) {
            continue;
        }
        writer.addFile(entry.first, entry.second);
    }
    writer.close();
    if (writer.status() != QZipWriter::NoError) {
        return Result<QByteArray>::error(QStringLiteral("backup.archive_failed"));
    }
    buffer.close();
    return Result<QByteArray>::ok(buffer.data());
#else
    Q_UNUSED(entries);
    return Result<QByteArray>::error(QStringLiteral("backup.zip_unavailable"));
#endif
}

Result<QHash<QString, QByteArray>> readZip(const QByteArray &zip)
{
#ifdef MINI_HAVE_ZIP
    QBuffer buffer;
    buffer.setData(zip);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return Result<QHash<QString, QByteArray>>::error(
            QStringLiteral("backup.archive_invalid"));
    }
    QZipReader reader(&buffer);
    if (!reader.isReadable()) {
        return Result<QHash<QString, QByteArray>>::error(
            QStringLiteral("backup.archive_invalid"));
    }

    qint64 total = 0;
    const QList<QZipReader::FileInfo> infos = reader.fileInfoList();
    for (const QZipReader::FileInfo &info : infos) {
        if (info.isDir) {
            continue;
        }
        total += info.size;
        if (total > kMaxArchiveBytes) {
            return Result<QHash<QString, QByteArray>>::error(
                QStringLiteral("backup.too_large"));
        }
    }

    QHash<QString, QByteArray> entries;
    for (const QZipReader::FileInfo &info : infos) {
        if (info.isDir) {
            continue;
        }
        const QByteArray data = reader.fileData(info.filePath);
        if (data.isEmpty() && info.size > 0) {
            return Result<QHash<QString, QByteArray>>::error(
                QStringLiteral("backup.archive_invalid"));
        }
        entries.insert(info.filePath, data);
    }
    return Result<QHash<QString, QByteArray>>::ok(entries);
#else
    Q_UNUSED(zip);
    return Result<QHash<QString, QByteArray>>::error(
        QStringLiteral("backup.zip_unavailable"));
#endif
}

QJsonObject messageToJson(const Message &message, const QString &mediaExtension)
{
    QJsonObject object;
    object.insert(QStringLiteral("client_msg_id"), message.clientMsgId);
    object.insert(QStringLiteral("chat_id"), message.chatId);
    object.insert(QStringLiteral("server_msg_id"), message.serverMsgId);
    object.insert(QStringLiteral("sender_login"), message.senderLogin);
    object.insert(QStringLiteral("body"), message.body);
    object.insert(QStringLiteral("reply_to"), message.replyToClientMsgId);
    object.insert(QStringLiteral("kind"), mediaKindToString(message.kind));
    object.insert(QStringLiteral("media_id"), message.mediaId);
    object.insert(QStringLiteral("media_name"), message.mediaName);
    object.insert(QStringLiteral("media_size"), message.mediaSize);
    object.insert(QStringLiteral("media_duration_ms"), message.mediaDurationMs);
    object.insert(QStringLiteral("state"), messageStateToString(message.state));
    object.insert(QStringLiteral("outgoing"), message.outgoing);
    object.insert(QStringLiteral("local_ts"), message.localTs);
    object.insert(QStringLiteral("server_ts"), message.serverTs);
    object.insert(QStringLiteral("edited_at"), message.editedAt);
    object.insert(QStringLiteral("forwarded_from"), message.forwardedFrom);
    object.insert(QStringLiteral("expires_at"), message.expiresAt);
    object.insert(QStringLiteral("poll_id"), message.pollId);
    object.insert(QStringLiteral("system"), message.system);
    if (!mediaExtension.isEmpty()) {
        object.insert(QStringLiteral("media_ext"), mediaExtension);
    }
    return object;
}

bool messageFromJson(const QJsonObject &object, Message *message,
                     QString *mediaExtension)
{
    if (!message) {
        return false;
    }
    Message value;
    value.clientMsgId = object.value(QStringLiteral("client_msg_id")).toString();
    value.chatId = object.value(QStringLiteral("chat_id")).toString();
    if (value.clientMsgId.isEmpty() || value.chatId.isEmpty()) {
        return false;
    }
    value.serverMsgId = object.value(QStringLiteral("server_msg_id")).toString();
    value.senderLogin = object.value(QStringLiteral("sender_login")).toString();
    value.body = object.value(QStringLiteral("body")).toString();
    value.replyToClientMsgId = object.value(QStringLiteral("reply_to")).toString();
    value.kind = mediaKindFromString(object.value(QStringLiteral("kind")).toString());
    value.mediaId = object.value(QStringLiteral("media_id")).toString();
    value.mediaName = object.value(QStringLiteral("media_name")).toString();
    value.mediaSize = object.value(QStringLiteral("media_size")).toInteger();
    value.mediaDurationMs =
        object.value(QStringLiteral("media_duration_ms")).toInteger();
    value.state = messageStateFromString(object.value(QStringLiteral("state")).toString());
    value.outgoing = object.value(QStringLiteral("outgoing")).toBool(true);
    value.localTs = object.value(QStringLiteral("local_ts")).toInteger();
    value.serverTs = object.value(QStringLiteral("server_ts")).toInteger();
    value.editedAt = object.value(QStringLiteral("edited_at")).toInteger();
    value.forwardedFrom = object.value(QStringLiteral("forwarded_from")).toString();
    value.expiresAt = object.value(QStringLiteral("expires_at")).toInteger();
    value.pollId = object.value(QStringLiteral("poll_id")).toString();
    value.system = object.value(QStringLiteral("system")).toBool(false);
    *message = value;
    if (mediaExtension) {
        *mediaExtension = object.value(QStringLiteral("media_ext")).toString();
    }
    return true;
}

QJsonObject chatToJson(const Chat &chat)
{
    QJsonObject object;
    object.insert(QStringLiteral("chat_id"), chat.chatId);
    object.insert(QStringLiteral("kind"), chat.kind);
    object.insert(QStringLiteral("peer_login"), chat.peerLogin);
    object.insert(QStringLiteral("title"), chat.title);
    object.insert(QStringLiteral("last_message_ts"), chat.lastMessageTs);
    object.insert(QStringLiteral("last_message_preview"), chat.lastMessagePreview);
    object.insert(QStringLiteral("unread_count"), chat.unreadCount);
    object.insert(QStringLiteral("pinned"), chat.pinned);
    object.insert(QStringLiteral("muted"), chat.muted);
    object.insert(QStringLiteral("updated_at"), chat.updatedAt);
    object.insert(QStringLiteral("muted_until"), chat.mutedUntil);
    object.insert(QStringLiteral("archived"), chat.archived);
    object.insert(QStringLiteral("expires_after_ms"), chat.expiresAfterMs);
    object.insert(QStringLiteral("is_bot"), chat.isBot);
    object.insert(QStringLiteral("e2e"), chat.e2e);
    return object;
}

bool chatFromJson(const QJsonObject &object, Chat *chat)
{
    if (!chat) {
        return false;
    }
    Chat value;
    value.chatId = object.value(QStringLiteral("chat_id")).toString();
    if (value.chatId.isEmpty()) {
        return false;
    }
    value.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("direct"));
    value.peerLogin = object.value(QStringLiteral("peer_login")).toString();
    value.title = object.value(QStringLiteral("title")).toString();
    value.lastMessageTs = object.value(QStringLiteral("last_message_ts")).toInteger();
    value.lastMessagePreview =
        object.value(QStringLiteral("last_message_preview")).toString();
    value.unreadCount = object.value(QStringLiteral("unread_count")).toInt();
    value.pinned = object.value(QStringLiteral("pinned")).toBool(false);
    value.muted = object.value(QStringLiteral("muted")).toBool(false);
    value.updatedAt = object.value(QStringLiteral("updated_at")).toInteger();
    value.mutedUntil = object.value(QStringLiteral("muted_until")).toInteger();
    value.archived = object.value(QStringLiteral("archived")).toBool(false);
    value.expiresAfterMs =
        object.value(QStringLiteral("expires_after_ms")).toInteger();
    value.isBot = object.value(QStringLiteral("is_bot")).toBool(false);
    value.e2e = object.value(QStringLiteral("e2e")).toBool(true);
    *chat = value;
    return true;
}

bool safeMediaId(const QString &mediaId)
{
    if (mediaId.isEmpty() || mediaId.size() > 200 || mediaId.startsWith(QLatin1Char('.'))) {
        return false;
    }
    for (const QChar ch : mediaId) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-')
            && ch != QLatin1Char('_')) {
            return false;
        }
    }
    return mediaId != QLatin1String(".") && mediaId != QLatin1String("..");
}

QString safeMediaExtension(const QString &extension)
{
    QString clean = extension.trimmed().toLower();
    while (clean.startsWith(QLatin1Char('.'))) {
        clean.remove(0, 1);
    }
    if (clean.isEmpty() || clean.size() > 16) {
        return QStringLiteral("bin");
    }
    for (const QChar ch : clean) {
        if (!ch.isLetterOrNumber()) {
            return QStringLiteral("bin");
        }
    }
    return clean;
}

} // namespace mini::services::backup
