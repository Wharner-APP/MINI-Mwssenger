#include "ui/StickerPanel.h"

#include "services/StickerService.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

using services::StickerItem;
using services::StickerPack;
using services::StickerService;

StickerPanel::StickerPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("stickerPanel"));
    setMinimumHeight(240);

    m_packs = new QComboBox(this);
    m_packs->setObjectName(QStringLiteral("stickerPackBox"));
    m_packs->setAccessibleName(tr("Sticker pack"));
    m_packs->setMinimumWidth(160);

    m_install = new QPushButton(tr("Install…"), this);
    m_install->setObjectName(QStringLiteral("stickerInstallButton"));
    m_install->setToolTip(tr("Install a .ministicker pack"));
    m_remove = new QPushButton(tr("Remove"), this);
    m_remove->setObjectName(QStringLiteral("stickerRemoveButton"));

    auto *top = new QHBoxLayout();
    top->setContentsMargins(8, 6, 8, 0);
    top->setSpacing(8);
    top->addWidget(m_packs, 1);
    top->addWidget(m_install, 0);
    top->addWidget(m_remove, 0);

    m_grid = new QListWidget(this);
    m_grid->setObjectName(QStringLiteral("stickerGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setMovement(QListView::Static);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setGridSize(QSize(80, 80));
    m_grid->setIconSize(QSize(72, 72));
    m_grid->setUniformItemSizes(true);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setFocusPolicy(Qt::NoFocus);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setAccessibleName(tr("Stickers"));

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("stickerPreview"));
    m_preview->setFixedSize(160, 160);
    m_preview->setAlignment(Qt::AlignCenter);

    auto *body = new QHBoxLayout();
    body->setContentsMargins(8, 4, 8, 8);
    body->setSpacing(8);
    body->addWidget(m_grid, 1);
    body->addWidget(m_preview, 0, Qt::AlignTop);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addLayout(body, 1);

    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item || !m_service) {
            return;
        }
        const QString packId = currentPackId();
        const QString stickerId = item->data(Qt::UserRole).toString();
        if (packId.isEmpty() || stickerId.isEmpty()) {
            return;
        }
        const QPixmap pixmap =
            m_service->stickerPixmap(packId, stickerId, 160);
        if (!pixmap.isNull()) {
            m_preview->setPixmap(pixmap);
        }
        emit stickerSelected(packId, stickerId);
    });
    connect(m_packs, &QComboBox::currentIndexChanged, this, [this](int index) {
        fillPack(index);
    });
    connect(m_install, &QPushButton::clicked, this, &StickerPanel::installRequested);
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QString packId = currentPackId();
        if (!packId.isEmpty()) {
            emit removeRequested(packId);
        }
    });
    connect(m_grid, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                if (!current || !m_service) {
                    return;
                }
                const QPixmap pixmap = m_service->stickerPixmap(
                    currentPackId(), current->data(Qt::UserRole).toString(), 160);
                m_preview->setPixmap(pixmap);
            });
    m_preview->setText(tr("Preview"));
    m_preview->setProperty("miniRole", QStringLiteral("caption"));
}

void StickerPanel::setService(StickerService *service)
{
    if (m_service == service) {
        return;
    }
    if (m_service) {
        disconnect(m_service, nullptr, this, nullptr);
    }
    m_service = service;
    if (m_service) {
        connect(m_service, &StickerService::packsChanged, this, &StickerPanel::reload);
    }
    reload();
}

void StickerPanel::reload()
{
    m_packs->blockSignals(true);
    m_packs->clear();
    if (m_service) {
        for (const StickerPack &pack : m_service->packs()) {
            m_packs->addItem(pack.title, pack.id);
        }
    }
    m_packs->blockSignals(false);
    fillPack(m_packs->count() > 0 ? 0 : -1);
}

void StickerPanel::fillPack(int index)
{
    m_grid->clear();
    m_preview->clear();
    if (!m_service || index < 0 || index >= m_packs->count()) {
        m_remove->setEnabled(false);
        return;
    }
    const QString packId = m_packs->itemData(index).toString();
    const auto pack = m_service->pack(packId);
    if (!pack.has_value()) {
        m_remove->setEnabled(false);
        return;
    }
    for (const StickerItem &sticker : pack->stickers) {
        const QPixmap pixmap = m_service->stickerPixmap(packId, sticker.id, 72);
        auto *item = new QListWidgetItem(QIcon(pixmap), QString(), m_grid);
        item->setData(Qt::UserRole, sticker.id);
        item->setToolTip(sticker.title);
        item->setSizeHint(QSize(80, 80));
    }
    m_remove->setEnabled(!pack->builtIn);
    m_preview->setText(tr("Preview"));
}

QString StickerPanel::currentPackId() const
{
    const int index = m_packs->currentIndex();
    if (index < 0 || index >= m_packs->count()) {
        return {};
    }
    return m_packs->itemData(index).toString();
}

} // namespace mini
