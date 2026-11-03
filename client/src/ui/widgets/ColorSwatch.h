#pragma once

#include <QAbstractButton>
#include <QColor>

namespace mini {

// Round colour chip used by the accent picker.
class ColorSwatch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ColorSwatch(const QColor &color, QWidget *parent = nullptr);

    [[nodiscard]] QColor color() const { return m_color; }
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color;
};

} // namespace mini
