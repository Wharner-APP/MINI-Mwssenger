#pragma once

#include <QAbstractButton>

class QPropertyAnimation;

namespace mini {

// Animated on/off switch (120 ms, no bounce).
class Toggle : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit Toggle(QWidget *parent = nullptr);

    [[nodiscard]] qreal position() const { return m_position; }
    void setPosition(qreal position);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    qreal m_position = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

} // namespace mini
