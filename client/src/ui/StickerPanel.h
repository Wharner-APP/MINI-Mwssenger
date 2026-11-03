#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace mini {

namespace services {
class StickerService;
}

// §33.14 / §40.9: pack switcher, 72 px sticker grid and a 160 px preview.
class StickerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StickerPanel(QWidget *parent = nullptr);

    void setService(services::StickerService *service);
    [[nodiscard]] services::StickerService *service() const { return m_service; }
    void reload();

    [[nodiscard]] QComboBox *packBox() const { return m_packs; }
    [[nodiscard]] QListWidget *grid() const { return m_grid; }
    [[nodiscard]] QLabel *preview() const { return m_preview; }
    [[nodiscard]] QPushButton *installButton() const { return m_install; }
    [[nodiscard]] QPushButton *removeButton() const { return m_remove; }
    [[nodiscard]] QString currentPackId() const;

signals:
    void stickerSelected(const QString &packId, const QString &stickerId);
    void installRequested();
    void removeRequested(const QString &packId);

private:
    void fillPack(int index);

    services::StickerService *m_service = nullptr;
    QComboBox *m_packs = nullptr;
    QListWidget *m_grid = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_install = nullptr;
    QPushButton *m_remove = nullptr;
};

} // namespace mini
