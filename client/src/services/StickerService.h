#pragma once

#include "core/Result.h"

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QVector>

#include <optional>

namespace mini::services {

struct StickerItem
{
    QString id;
    QString title;
    QString file;
};

struct StickerPack
{
    QString id;
    QString title;
    QString author;
    QString cover;
    QVector<StickerItem> stickers;
    bool builtIn = true;
    QString sourceDir;
};

// §40.9 / §33.14: bundled packs from resources/stickers plus packs the user
// installed from a .ministicker archive. Pixmaps are decoded once and cached;
// the chat bubble asks for 160 px, the panel for 72 px.
class StickerService : public QObject
{
    Q_OBJECT

public:
    explicit StickerService(QObject *parent = nullptr);

    // Test seam; the defaults are ":/stickers" and
    // <data>/stickers (the shell calls setUserDir with Paths::dataDir()).
    void setBuiltInRoot(const QString &root);
    void setUserDir(const QString &dir);
    [[nodiscard]] QString userDir() const { return m_userDir; }

    void reload();
    [[nodiscard]] QVector<StickerPack> packs() const { return m_packs; }
    [[nodiscard]] std::optional<StickerPack> pack(const QString &id) const;
    [[nodiscard]] bool isInstalled(const QString &id) const;
    [[nodiscard]] QPixmap stickerPixmap(const QString &packId, const QString &stickerId,
                                        int size) const;
    [[nodiscard]] QPixmap coverPixmap(const StickerPack &pack, int size) const;

    // Installs a .ministicker ZIP. The archive must contain manifest.json and
    // the files it references. Returns the installed pack id through *packId.
    Result<bool> install(const QString &path, QString *packId = nullptr);
    Result<bool> remove(const QString &packId);

signals:
    void packsChanged();

private:
    static std::optional<StickerPack> readManifest(const QString &dir, bool builtIn);
    void loadRoot(const QString &root, bool builtIn);
    [[nodiscard]] static QString cacheKey(const QString &packId, const QString &stickerId);

    QVector<StickerPack> m_packs;
    QHash<QString, QPixmap> m_pixmaps;
    QString m_builtInRoot = QStringLiteral(":/stickers");
    QString m_userDir;
};

} // namespace mini::services
