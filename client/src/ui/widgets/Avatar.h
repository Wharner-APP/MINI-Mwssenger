#pragma once

#include <QColor>
#include <QWidget>

namespace mini {

class Avatar : public QWidget
{
    Q_OBJECT

public:
    explicit Avatar(QWidget *parent = nullptr);

    void setInitials(const QString &initials);
    void setColor(const QColor &color);

    [[nodiscard]] QString initials() const { return m_initials; }
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_initials;
    QColor m_color;
};

} // namespace mini
