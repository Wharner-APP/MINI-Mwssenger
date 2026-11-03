#pragma once

#include "domain/Media.h"

#include <QDialog>
#include <QPixmap>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace mini {

class ImageCanvas;

struct MediaViewerItem
{
    QString mediaId;
    QString title;
    QString subtitle;
    QString path; // decrypted local file, empty until downloaded
    QPixmap image;
    MediaKind kind = MediaKind::Photo;
    qint64 durationMs = 0;
};

// §33.15 / §40.3: full-screen photo viewer. Wheel zoom 10-500%, drag to pan,
// arrows move through the album, Esc closes. Video uses the same shell.
class ImageCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    void setImage(const QPixmap &image);
    [[nodiscard]] QPixmap image() const { return m_image; }
    [[nodiscard]] double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void resetView();

signals:
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateFit();

    QPixmap m_image;
    double m_zoom = 1.0;
    double m_fitScale = 1.0;
    QPointF m_offset;
    QPointF m_dragOrigin;
    bool m_dragging = false;
};

class MediaViewer : public QDialog
{
    Q_OBJECT

public:
    explicit MediaViewer(QWidget *parent = nullptr);

    void setItems(const QVector<MediaViewerItem> &items, int index = 0);
    [[nodiscard]] const QVector<MediaViewerItem> &items() const { return m_items; }
    [[nodiscard]] int currentIndex() const { return m_index; }
    [[nodiscard]] double zoom() const;
    [[nodiscard]] ImageCanvas *canvas() const { return m_canvas; }
    [[nodiscard]] QStackedWidget *stack() const { return m_stack; }
    [[nodiscard]] QToolButton *saveButton() const { return m_save; }
    [[nodiscard]] QToolButton *deleteButton() const { return m_delete; }
    [[nodiscard]] QToolButton *showInChatButton() const { return m_showInChat; }
    [[nodiscard]] QToolButton *nextButton() const { return m_next; }
    [[nodiscard]] QToolButton *previousButton() const { return m_previous; }

    void showCurrent();
    void next();
    void previous();

signals:
    void saveRequested(const QString &mediaId);
    void deleteRequested(const QString &mediaId);
    void showInChatRequested(const QString &mediaId);
    void openExternalRequested(const QString &mediaId);
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void updateChrome();

    QVector<MediaViewerItem> m_items;
    int m_index = 0;
    QLabel *m_title = nullptr;
    QLabel *m_counter = nullptr;
    QLabel *m_videoHint = nullptr;
    QPushButton *m_openExternal = nullptr;
    QStackedWidget *m_stack = nullptr;
    ImageCanvas *m_canvas = nullptr;
    QWidget *m_videoPanel = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_delete = nullptr;
    QToolButton *m_showInChat = nullptr;
    QToolButton *m_close = nullptr;
};

} // namespace mini
