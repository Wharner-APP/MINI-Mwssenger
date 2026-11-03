#pragma once

#include <QWidget>

class QComboBox;
class QGridLayout;
class QLineEdit;
class QScrollArea;

namespace mini {

// §40.8: bundled GIF catalog, two-column grid and a category filter.
class GifPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GifPanel(QWidget *parent = nullptr);

    void reload();
    [[nodiscard]] QLineEdit *searchEdit() const { return m_search; }
    [[nodiscard]] QComboBox *categoryBox() const { return m_categories; }
    [[nodiscard]] int itemCount() const { return m_itemCount; }
    [[nodiscard]] QWidget *gridHost() const { return m_host; }

signals:
    void gifSelected(const QString &gifId);

private:
    void rebuild();

    QLineEdit *m_search = nullptr;
    QComboBox *m_categories = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_host = nullptr;
    QGridLayout *m_grid = nullptr;
    int m_itemCount = 0;
};

} // namespace mini
