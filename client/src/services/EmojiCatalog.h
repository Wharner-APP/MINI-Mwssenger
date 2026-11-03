#pragma once

#include <QFont>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mini::services {

struct EmojiItem
{
    QString character;
    QString name;
    QString keywords;
};

struct EmojiCategory
{
    QString id;
    QString title;
    QVector<EmojiItem> emojis;
};

// §35.3 / §11.5 #91-92: eight categories, search by name and keywords, and a
// recent list. The catalog is bundled as JSON so no network call is needed.
class EmojiCatalog
{
public:
    static EmojiCatalog &instance();

    void reload();
    [[nodiscard]] bool isLoaded() const { return m_loaded; }
    [[nodiscard]] const QVector<EmojiCategory> &categories() const { return m_categories; }
    [[nodiscard]] QVector<EmojiItem> search(const QString &query, int limit = 120) const;

    [[nodiscard]] QStringList recent() const { return m_recent; }
    void setRecent(const QStringList &characters);
    void addRecent(const QString &character);

    // Segoe UI Emoji on Windows, Noto Color Emoji elsewhere, falling back to
    // the application font so tests still render glyphs.
    [[nodiscard]] static QFont emojiFont(int pixelSize);

private:
    EmojiCatalog() = default;
    void ensureLoaded();

    QVector<EmojiCategory> m_categories;
    QStringList m_recent;
    bool m_loaded = false;
};

} // namespace mini::services
