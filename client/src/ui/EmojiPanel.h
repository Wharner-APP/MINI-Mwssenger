#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;

namespace mini {

// §35.3 / §11.5 #91-92: eight categories, recent list, search by name and
// insertion at the current cursor position (the composer owns the editor).
class EmojiPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EmojiPanel(QWidget *parent = nullptr);

    [[nodiscard]] QComboBox *categoryBox() const { return m_categories; }
    [[nodiscard]] QLineEdit *searchEdit() const { return m_search; }
    [[nodiscard]] QListWidget *grid() const { return m_grid; }
    [[nodiscard]] QStringList recent() const { return m_recent; }

    void setRecent(const QStringList &characters);
    void reload();
    [[nodiscard]] int itemCount() const;

signals:
    void emojiSelected(const QString &character);
    void recentChanged(const QStringList &characters);

private:
    void fillCategory(int index);
    void refill();
    void insertEmoji(const QString &character);

    QComboBox *m_categories = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_grid = nullptr;
    QStringList m_recent;
    bool m_filling = false;
};

} // namespace mini
