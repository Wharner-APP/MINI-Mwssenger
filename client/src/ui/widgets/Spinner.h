#pragma once

#include <QWidget>

class QTimer;

namespace mini {

class Spinner : public QWidget
{
    Q_OBJECT

public:
    explicit Spinner(QWidget *parent = nullptr);

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer *m_timer = nullptr;
    int m_angle = 0;
};

} // namespace mini
