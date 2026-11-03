#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QTimer;

namespace mini {

// §46.1: small in-app toast shown when the window is active but another chat
// is open. It never takes focus and disappears on its own; clicking it opens
// the chat that produced the notification.
class Toast : public QFrame
{
    Q_OBJECT

public:
    explicit Toast(QWidget *parent = nullptr);

    void showMessage(const QString &title, const QString &body, int timeoutMs = 5000);
    void dismiss();

    [[nodiscard]] QLabel *titleLabel() const { return m_title; }
    [[nodiscard]] QLabel *bodyLabel() const { return m_body; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void reposition();

    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    QTimer *m_timer = nullptr;
};

} // namespace mini
