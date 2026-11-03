#include "services/EmojiCatalog.h"

#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mini::services {

namespace {
constexpr int kMaxRecent = 24;
constexpr int kMaxItemsPerCategory = 512;
} // namespace

EmojiCatalog &EmojiCatalog::instance()
{
    static EmojiCatalog catalog;
    return catalog;
}

void EmojiCatalog::reload()
{
    m_categories.clear();
    QFile file(QStringLiteral(":/emoji/emoji.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonArray categories =
            document.object().value(QStringLiteral("categories")).toArray();
        for (const QJsonValue &value : categories) {
            const QJsonObject object = value.toObject();
            EmojiCategory category;
            category.id = object.value(QStringLiteral("id")).toString();
            category.title = object.value(QStringLiteral("title")).toString();
            const QJsonArray emojis = object.value(QStringLiteral("emojis")).toArray();
            for (const QJsonValue &itemValue : emojis) {
                const QJsonObject item = itemValue.toObject();
                EmojiItem emoji;
                emoji.character = item.value(QStringLiteral("char")).toString();
                emoji.name = item.value(QStringLiteral("name")).toString();
                emoji.keywords = item.value(QStringLiteral("keywords")).toString();
                if (emoji.character.isEmpty()) {
                    continue;
                }
                category.emojis.append(emoji);
                if (category.emojis.size() >= kMaxItemsPerCategory) {
                    break;
                }
            }
            if (!category.id.isEmpty() && !category.emojis.isEmpty()) {
                if (category.title.isEmpty()) {
                    category.title = category.id;
                }
                m_categories.append(category);
            }
        }
    }
    m_loaded = true;
}

void EmojiCatalog::ensureLoaded()
{
    if (!m_loaded) {
        reload();
    }
}

QVector<EmojiItem> EmojiCatalog::search(const QString &query, int limit) const
{
    const_cast<EmojiCatalog *>(this)->ensureLoaded();
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) {
        return {};
    }
    QVector<EmojiItem> result;
    for (const EmojiCategory &category : m_categories) {
        for (const EmojiItem &emoji : category.emojis) {
            if (emoji.character == needle || emoji.name.toLower().contains(needle)
                || emoji.keywords.toLower().contains(needle)) {
                result.append(emoji);
                if (result.size() >= limit) {
                    return result;
                }
            }
        }
    }
    return result;
}

void EmojiCatalog::setRecent(const QStringList &characters)
{
    m_recent.clear();
    for (const QString &character : characters) {
        if (character.isEmpty() || m_recent.contains(character)) {
            continue;
        }
        m_recent.append(character);
        if (m_recent.size() >= kMaxRecent) {
            break;
        }
    }
}

void EmojiCatalog::addRecent(const QString &character)
{
    if (character.isEmpty()) {
        return;
    }
    m_recent.removeAll(character);
    m_recent.prepend(character);
    while (m_recent.size() > kMaxRecent) {
        m_recent.removeLast();
    }
}

QFont EmojiCatalog::emojiFont(int pixelSize)
{
    static const QStringList candidates{
        QStringLiteral("Segoe UI Emoji"),
        QStringLiteral("Noto Color Emoji"),
        QStringLiteral("Apple Color Emoji"),
        QStringLiteral("Segoe UI Symbol"),
    };
    const QStringList families = QFontDatabase::families();
    QFont font;
    for (const QString &candidate : candidates) {
        if (families.contains(candidate)) {
            font.setFamily(candidate);
            break;
        }
    }
    font.setPixelSize(pixelSize);
    return font;
}

} // namespace mini::services
