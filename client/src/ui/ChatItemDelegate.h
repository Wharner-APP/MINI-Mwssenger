#pragma once

#include <QColor>
#include <QStyledItemDelegate>

namespace mini {

class ChatItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ChatItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;

    // Initials colour with >= 4.5:1 contrast on a 20% accent tint.
    [[nodiscard]] static QColor initialsColor(const QColor &accent,
                                              const QColor &background);
};

} // namespace mini
