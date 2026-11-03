#include "ui/EmojiPanel.h"

#include "services/EmojiCatalog.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QVBoxLayout>

namespace mini {

using services::EmojiCatalog;
using services::EmojiItem;

EmojiPanel::EmojiPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("emojiPanel"));
    setMinimumHeight(240);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("emojiSearch"));
    m_search->setPlaceholderText(tr("Search emoji…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search emoji"));

    m_categories = new QComboBox(this);
    m_categories->setObjectName(QStringLiteral("emojiCategoryBox"));
    m_categories->setAccessibleName(tr("Emoji category"));
    m_categories->setMinimumWidth(150);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(8, 6, 8, 0);
    top->setSpacing(8);
    top->addWidget(m_search, 1);
    top->addWidget(m_categories, 0);

    m_grid = new QListWidget(this);
    m_grid->setObjectName(QStringLiteral("emojiGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setMovement(QListView::Static);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setGridSize(QSize(42, 42));
    m_grid->setIconSize(QSize(28, 28));
    m_grid->setUniformItemSizes(true);
    m_grid->setSelectionMode(QAbstractItemView::NoSelection);
    m_grid->setFocusPolicy(Qt::NoFocus);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setAccessibleName(tr("Emoji"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addWidget(m_grid, 1);

    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            insertEmoji(item->data(Qt::UserRole).toString());
        }
    });
    connect(m_categories, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_filling) {
            fillCategory(index);
        }
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { refill(); });

    reload();
}

void EmojiPanel::reload()
{
    EmojiCatalog &catalog = EmojiCatalog::instance();
    catalog.reload();

    m_filling = true;
    m_categories->clear();
    m_categories->addItem(tr("Recent"));
    for (const services::EmojiCategory &category : catalog.categories()) {
        m_categories->addItem(category.title);
    }
    m_filling = false;
    m_categories->setCurrentIndex(0);
    fillCategory(0);
}

void EmojiPanel::setRecent(const QStringList &characters)
{
    m_recent = characters;
    EmojiCatalog::instance().setRecent(characters);
    if (m_categories->currentIndex() == 0 && m_search->text().trimmed().isEmpty()) {
        fillCategory(0);
    }
}

void EmojiPanel::fillCategory(int index)
{
    m_grid->clear();
    const EmojiCatalog &catalog = EmojiCatalog::instance();
    const QFont font = EmojiCatalog::emojiFont(22);

    if (index <= 0) {
        for (const QString &character : m_recent) {
            auto *item = new QListWidgetItem(character, m_grid);
            item->setFont(font);
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, character);
            item->setToolTip(character);
        }
        return;
    }
    const int categoryIndex = index - 1;
    if (categoryIndex >= catalog.categories().size()) {
        return;
    }
    for (const EmojiItem &emoji : catalog.categories().at(categoryIndex).emojis) {
        auto *item = new QListWidgetItem(emoji.character, m_grid);
        item->setFont(font);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, emoji.character);
        item->setToolTip(emoji.name);
    }
}

void EmojiPanel::refill()
{
    const QString query = m_search->text().trimmed();
    if (query.isEmpty()) {
        fillCategory(m_categories->currentIndex());
        return;
    }
    m_grid->clear();
    const QFont font = EmojiCatalog::emojiFont(22);
    for (const EmojiItem &emoji : EmojiCatalog::instance().search(query)) {
        auto *item = new QListWidgetItem(emoji.character, m_grid);
        item->setFont(font);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, emoji.character);
        item->setToolTip(emoji.name);
    }
}

void EmojiPanel::insertEmoji(const QString &character)
{
    if (character.isEmpty()) {
        return;
    }
    EmojiCatalog &catalog = EmojiCatalog::instance();
    catalog.addRecent(character);
    m_recent = catalog.recent();
    emit recentChanged(m_recent);
    emit emojiSelected(character);
    if (m_categories->currentIndex() == 0 && m_search->text().trimmed().isEmpty()) {
        fillCategory(0);
    }
}

int EmojiPanel::itemCount() const
{
    return m_grid->count();
}

} // namespace mini
