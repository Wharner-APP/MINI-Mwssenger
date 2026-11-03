#include "ui/GifPanel.h"

#include "services/GifCatalog.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace mini {

using services::GifCatalog;
using services::GifItem;

GifPanel::GifPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gifPanel"));
    setMinimumHeight(240);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("gifSearch"));
    m_search->setPlaceholderText(tr("Search GIF…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search GIF"));

    m_categories = new QComboBox(this);
    m_categories->setObjectName(QStringLiteral("gifCategoryBox"));
    m_categories->setAccessibleName(tr("GIF category"));
    m_categories->setMinimumWidth(150);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(8, 6, 8, 0);
    top->setSpacing(8);
    top->addWidget(m_search, 1);
    top->addWidget(m_categories, 0);

    m_host = new QWidget(this);
    m_grid = new QGridLayout(m_host);
    m_grid->setContentsMargins(8, 6, 8, 8);
    m_grid->setSpacing(6);
    m_grid->setColumnStretch(0, 1);
    m_grid->setColumnStretch(1, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("gifScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(m_host);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addWidget(m_scroll, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_categories, &QComboBox::currentIndexChanged, this, [this] { rebuild(); });

    reload();
}

void GifPanel::reload()
{
    GifCatalog::instance().reload();
    const QString selected = m_categories->currentText();
    m_categories->blockSignals(true);
    m_categories->clear();
    m_categories->addItem(tr("All"));
    for (const QString &category : GifCatalog::instance().categories()) {
        m_categories->addItem(category);
    }
    const int restored = m_categories->findText(selected);
    m_categories->setCurrentIndex(restored > 0 ? restored : 0);
    m_categories->blockSignals(false);
    rebuild();
}

void GifPanel::rebuild()
{
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_itemCount = 0;
    const QString query = m_search->text().trimmed().toLower();
    const QString category = m_categories->currentIndex() > 0
        ? m_categories->currentText()
        : QString();
    const GifCatalog &catalog = GifCatalog::instance();
    int index = 0;
    for (const GifItem &item : catalog.items()) {
        if (!category.isEmpty() && item.category != category) {
            continue;
        }
        if (!query.isEmpty() && !item.title.toLower().contains(query)
            && !item.category.toLower().contains(query)) {
            continue;
        }
        auto *button = new QPushButton(m_host);
        button->setObjectName(QStringLiteral("gifItem_") + item.id);
        button->setProperty("miniRole", QStringLiteral("gifItem"));
        button->setIcon(QIcon(catalog.cover(item.id, 160)));
        button->setIconSize(QSize(160, 96));
        button->setText(item.title);
        button->setToolTip(item.title);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        connect(button, &QPushButton::clicked, this,
                [this, id = item.id] { emit gifSelected(id); });
        m_grid->addWidget(button, index / 2, index % 2);
        ++index;
    }
    m_itemCount = index;
    m_grid->setRowStretch(index / 2 + 1, 1);
}

} // namespace mini
