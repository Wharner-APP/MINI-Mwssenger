#include "services/StickerService.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#ifdef MINI_HAVE_ZIP
#include <QBuffer>
#include <QtCore/private/qzipreader_p.h>
#endif

namespace mini::services {

namespace {

constexpr int kMaxStickers = 64;
constexpr qint64 kMaxStickerBytes = 5 * 1024 * 1024;
constexpr qint64 kMaxManifestBytes = 1024 * 1024;

QString safeFolderName(const QString &id)
{
    QString name;
    name.reserve(id.size());
    for (const QChar ch : id) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('-')
            || ch == QLatin1Char('_')) {
            name.append(ch);
        } else {
            name.append(QLatin1Char('_'));
        }
    }
    while (name.startsWith(QLatin1Char('.'))) {
        name.remove(0, 1);
    }
    return name.left(64);
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1String("..")) || path.contains(QLatin1Char(':'))) {
        return false;
    }
    const QString clean = QDir::cleanPath(path);
    return !clean.isEmpty() && !clean.startsWith(QLatin1String(".."))
        && !QDir::isAbsolutePath(clean);
}

QJsonObject parseManifestBytes(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? document.object() : QJsonObject();
}

bool parsePack(const QJsonObject &object, StickerPack *pack)
{
    if (!pack) {
        return false;
    }
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    StickerPack parsed;
    parsed.id = id;
    parsed.title = object.value(QStringLiteral("title")).toString().trimmed();
    if (parsed.title.isEmpty()) {
        parsed.title = id;
    }
    parsed.author = object.value(QStringLiteral("author")).toString().trimmed();
    parsed.cover = object.value(QStringLiteral("cover")).toString();

    const QJsonArray stickers = object.value(QStringLiteral("stickers")).toArray();
    if (stickers.isEmpty() || stickers.size() > kMaxStickers) {
        return false;
    }
    for (const QJsonValue &value : stickers) {
        const QJsonObject item = value.toObject();
        StickerItem sticker;
        sticker.id = item.value(QStringLiteral("id")).toString().trimmed();
        sticker.file = item.value(QStringLiteral("file")).toString().trimmed();
        sticker.title = item.value(QStringLiteral("title")).toString().trimmed();
        if (sticker.id.isEmpty() || !safeRelativePath(sticker.file)) {
            return false;
        }
        parsed.stickers.append(sticker);
    }
    if (!parsed.cover.isEmpty() && !safeRelativePath(parsed.cover)) {
        parsed.cover.clear();
    }
    *pack = parsed;
    return true;
}

} // namespace

StickerService::StickerService(QObject *parent) : QObject(parent)
{
    reload();
}

void StickerService::setBuiltInRoot(const QString &root)
{
    m_builtInRoot = root;
}

void StickerService::setUserDir(const QString &dir)
{
    m_userDir = dir;
    reload();
}

QString StickerService::cacheKey(const QString &packId, const QString &stickerId)
{
    return packId + QLatin1Char('\x1f') + stickerId;
}

std::optional<StickerPack> StickerService::readManifest(const QString &dir, bool builtIn)
{
    QFile manifest(QDir(dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = manifest.read(kMaxManifestBytes + 1);
    if (bytes.size() > kMaxManifestBytes) {
        return std::nullopt;
    }
    StickerPack pack;
    if (!parsePack(parseManifestBytes(bytes), &pack)) {
        return std::nullopt;
    }
    pack.builtIn = builtIn;
    pack.sourceDir = dir;
    return pack;
}

void StickerService::loadRoot(const QString &root, bool builtIn)
{
    const QDir dir(root);
    if (!dir.exists()) {
        return;
    }
    const QStringList folders =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        const QString packDir = dir.filePath(folder);
        auto pack = readManifest(packDir, builtIn);
        if (!pack.has_value()) {
            continue;
        }
        bool duplicate = false;
        for (const StickerPack &existing : m_packs) {
            if (existing.id == pack->id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            Log::warn(QStringLiteral("stickers_duplicate_pack"),
                      {{QStringLiteral("pack"), pack->id},
                       {QStringLiteral("dir"), packDir}});
            continue;
        }
        for (const StickerItem &sticker : pack->stickers) {
            QPixmap pixmap(QDir(packDir).filePath(sticker.file));
            if (!pixmap.isNull()) {
                m_pixmaps.insert(cacheKey(pack->id, sticker.id), pixmap);
            }
        }
        m_packs.append(*pack);
    }
}

void StickerService::reload()
{
    m_packs.clear();
    m_pixmaps.clear();
    loadRoot(m_builtInRoot, true);
    if (!m_userDir.isEmpty()) {
        loadRoot(m_userDir, false);
    }
    emit packsChanged();
}

std::optional<StickerPack> StickerService::pack(const QString &id) const
{
    for (const StickerPack &pack : m_packs) {
        if (pack.id == id) {
            return pack;
        }
    }
    return std::nullopt;
}

bool StickerService::isInstalled(const QString &id) const
{
    for (const StickerPack &pack : m_packs) {
        if (pack.id == id) {
            return !pack.builtIn;
        }
    }
    return false;
}

QPixmap StickerService::stickerPixmap(const QString &packId, const QString &stickerId,
                                      int size) const
{
    const QPixmap source = m_pixmaps.value(cacheKey(packId, stickerId));
    if (source.isNull()) {
        return {};
    }
    return source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap StickerService::coverPixmap(const StickerPack &pack, int size) const
{
    if (!pack.cover.isEmpty()) {
        if (const auto packStickers = this->pack(pack.id); packStickers.has_value()) {
            for (const StickerItem &sticker : packStickers->stickers) {
                if (sticker.file == pack.cover) {
                    return stickerPixmap(pack.id, sticker.id, size);
                }
            }
        }
    }
    if (!pack.stickers.isEmpty()) {
        return stickerPixmap(pack.id, pack.stickers.first().id, size);
    }
    return {};
}

Result<bool> StickerService::remove(const QString &packId)
{
    if (m_userDir.isEmpty()) {
        return Result<bool>::error(QStringLiteral("sticker_dir_unavailable"));
    }
    const auto existing = pack(packId);
    if (!existing.has_value()) {
        return Result<bool>::error(QStringLiteral("pack_not_found"));
    }
    if (existing->builtIn) {
        return Result<bool>::error(QStringLiteral("built_in_pack"));
    }
    const QString root = QDir(m_userDir).absolutePath();
    const QString target = QDir(existing->sourceDir).absolutePath();
    if (!target.startsWith(root + QLatin1Char('/'))) {
        return Result<bool>::error(QStringLiteral("pack_outside_user_dir"));
    }
    if (!QDir(target).removeRecursively()) {
        return Result<bool>::error(QStringLiteral("pack_remove_failed"));
    }
    reload();
    return Result<bool>::ok(true);
}

Result<bool> StickerService::install(const QString &path, QString *packId)
{
#ifdef MINI_HAVE_ZIP
    if (m_userDir.isEmpty()) {
        return Result<bool>::error(QStringLiteral("sticker_dir_unavailable"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<bool>::error(QStringLiteral("archive_unreadable"));
    }
    QZipReader reader(&file);
    if (!reader.isReadable()) {
        return Result<bool>::error(QStringLiteral("archive_invalid"));
    }

    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    QString manifestPath;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.isDir) {
            continue;
        }
        const QString name = entry.filePath;
        if (QFileInfo(name).fileName() != QLatin1String("manifest.json")
            || !safeRelativePath(name)) {
            continue;
        }
        if (manifestPath.isEmpty() || name.count(QLatin1Char('/'))
               < manifestPath.count(QLatin1Char('/'))) {
            manifestPath = name;
        }
    }
    if (manifestPath.isEmpty()) {
        return Result<bool>::error(QStringLiteral("manifest_missing"));
    }
    const QByteArray manifestBytes = reader.fileData(manifestPath);
    if (manifestBytes.isEmpty() || manifestBytes.size() > kMaxManifestBytes) {
        return Result<bool>::error(QStringLiteral("manifest_invalid"));
    }
    StickerPack pack;
    if (!parsePack(parseManifestBytes(manifestBytes), &pack)) {
        return Result<bool>::error(QStringLiteral("manifest_invalid"));
    }

    qint64 totalBytes = 0;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.isDir) {
            continue;
        }
        if (!safeRelativePath(entry.filePath)) {
            return Result<bool>::error(QStringLiteral("archive_unsafe_path"));
        }
        totalBytes += entry.size;
        if (totalBytes > 20 * 1024 * 1024) {
            return Result<bool>::error(QStringLiteral("archive_too_large"));
        }
    }
    QString baseDir = QFileInfo(manifestPath).path();
    if (baseDir == QLatin1String(".")) {
        baseDir.clear();
    }
    if (!baseDir.isEmpty()) {
        // The format keeps manifest.json at the archive root; a nested one
        // would be installed but never found by reload().
        return Result<bool>::error(QStringLiteral("manifest_not_at_root"));
    }
    const QString folder = safeFolderName(pack.id);
    if (folder.isEmpty()) {
        return Result<bool>::error(QStringLiteral("manifest_invalid"));
    }
    QDir userDir(m_userDir);
    if (!userDir.exists() && !userDir.mkpath(QStringLiteral("."))) {
        return Result<bool>::error(QStringLiteral("sticker_dir_unavailable"));
    }
    const QString target = userDir.filePath(folder);
    const QString staging =
        userDir.filePath(QStringLiteral(".install-")
                         + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(staging)) {
        return Result<bool>::error(QStringLiteral("sticker_dir_unavailable"));
    }
    auto fail = [&staging](const QString &code) {
        QDir(staging).removeRecursively();
        return Result<bool>::error(code);
    };

    if (!reader.extractAll(staging)) {
        return fail(QStringLiteral("sticker_extract_failed"));
    }
    auto extractedPath = [&](const QString &relative) {
        return QDir(staging).filePath(baseDir.isEmpty()
                                          ? relative
                                          : baseDir + QLatin1Char('/') + relative);
    };
    for (const StickerItem &sticker : pack.stickers) {
        const QFileInfo extracted(extractedPath(sticker.file));
        if (!extracted.isFile() || extracted.size() <= 0
            || extracted.size() > kMaxStickerBytes) {
            return fail(QStringLiteral("sticker_extract_failed:") + sticker.file);
        }
    }
    if (!pack.cover.isEmpty() && !QFileInfo::exists(extractedPath(pack.cover))) {
        pack.cover = pack.stickers.first().file;
    }
    if (QDir(target).exists() && !QDir(target).removeRecursively()) {
        return fail(QStringLiteral("pack_replace_failed"));
    }
    if (!QDir().rename(staging, target)) {
        return fail(QStringLiteral("pack_install_failed"));
    }
    reload();
    if (packId) {
        *packId = pack.id;
    }
    return Result<bool>::ok(true);
#else
    Q_UNUSED(path);
    Q_UNUSED(packId);
    return Result<bool>::error(QStringLiteral("zip_unavailable"));
#endif
}

} // namespace mini::services
