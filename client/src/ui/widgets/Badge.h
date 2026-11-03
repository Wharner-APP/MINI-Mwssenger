#pragma once

#include <QWidget>

namespace mini {

// Small unread counter pill. Counts above 99 are shown as "99+".
class Badge : public QWidget
{
    Q_OBJECT

public:
    explicit Badge(QWidget *parent = nullptr);

    void setCount(int count);
    [[nodiscard]] int count() const { return m_count; }
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_count = 0;
};

} // namespace mini
