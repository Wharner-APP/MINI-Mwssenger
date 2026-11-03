#include "ui/MediaViewer.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mini {

namespace {

QToolButton *viewerButton(QWidget *parent, const QString &objectName,
                          const QString &iconName, const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setFixedSize(40, 40);
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setAutoRaise(true);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: 20px; background: rgba(255,255,255,20); }"
        "QToolButton:hover { background: rgba(255,255,255,45); }"
        "QToolButton:disabled { background: transparent; }"));
    button->setIcon(Icons::icon(iconName, QColor(235, 235, 245), 20));
    return button;
}

} // namespace

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("imageCanvas"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
    setCursor(Qt::OpenHandCursor);
}

void ImageCanvas::setImage(const QPixmap &image)
{
    m_image = image;
    resetView();
    update();
}

void ImageCanvas::setZoom(double zoom)
{
    const double clamped = qBound(0.1, zoom, 5.0);
    if (qFuzzyCompare(clamped, m_zoom)) {
        return;
    }
    m_zoom = clamped;
    emit zoomChanged(m_zoom);
    update();
}

void ImageCanvas::resetView()
{
    m_zoom = 1.0;
    m_offset = QPointF();
    updateFit();
    emit zoomChanged(m_zoom);
    update();
}

void ImageCanvas::updateFit()
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        m_fitScale = 1.0;
        return;
    }
    const double availableWidth = qMax(1, width() - 48);
    const double availableHeight = qMax(1, height() - 48);
    m_fitScale = std::min(availableWidth / m_image.width(),
                          availableHeight / m_image.height());
}

void ImageCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(11, 11, 15));

    if (m_image.isNull()) {
        painter.setPen(QColor(160, 160, 175));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Image unavailable"));
        return;
    }

    const double scale = m_fitScale * m_zoom;
    const QSizeF target(m_image.width() * scale, m_image.height() * scale);
    const QPointF topLeft(width() / 2.0 - target.width() / 2.0 + m_offset.x(),
                          height() / 2.0 - target.height() / 2.0 + m_offset.y());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(QRectF(topLeft, target), m_image, QRectF(m_image.rect()));
}

void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
    setZoom(m_zoom * factor);
    event->accept();
}

void ImageCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_image.isNull()) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragOrigin = event->position();
    setCursor(Qt::ClosedHandCursor);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_offset += event->position() - m_dragOrigin;
    m_dragOrigin = event->position();
    update();
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    resetView();
}

void ImageCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateFit();
}

MediaViewer::MediaViewer(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("mediaViewer"));
    setWindowTitle(tr("Media viewer"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(QStringLiteral(
        "QDialog#mediaViewer { background: #0b0b0f; }"
        "QLabel { color: #e8e8ee; }"
        "QPushButton { color: #e8e8ee; background: rgba(255,255,255,18);"
        "  border: none; border-radius: 8px; padding: 8px 16px; }"
        "QPushButton:hover { background: rgba(255,255,255,40); }"));

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("mediaViewerTitle"));
    m_title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_counter = new QLabel(this);
    m_counter->setObjectName(QStringLiteral("mediaViewerCounter"));
    m_counter->setStyleSheet(QStringLiteral("color: #9a9aa8;"));

    m_close = viewerButton(this, QStringLiteral("mediaViewerClose"),
                           QStringLiteral("close"), tr("Close (Esc)"));
    connect(m_close, &QToolButton::clicked, this, &QDialog::close);

    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(20, 14, 20, 8);
    topBar->setSpacing(12);
    topBar->addWidget(m_title, 1);
    topBar->addWidget(m_counter, 0);
    topBar->addWidget(m_close, 0);

    m_canvas = new ImageCanvas(this);
    m_videoPanel = new QWidget(this);
    m_videoPanel->setObjectName(QStringLiteral("mediaViewerVideoPanel"));
    m_videoHint = new QLabel(m_videoPanel);
    m_videoHint->setAlignment(Qt::AlignCenter);
    m_videoHint->setStyleSheet(QStringLiteral("color: #c8c8d4;"));
    m_openExternal = new QPushButton(tr("Open in an external player"), m_videoPanel);
    m_openExternal->setObjectName(QStringLiteral("mediaViewerOpenExternal"));
    auto *videoLayout = new QVBoxLayout(m_videoPanel);
    videoLayout->setContentsMargins(24, 24, 24, 24);
    videoLayout->setSpacing(16);
    videoLayout->addStretch(1);
    videoLayout->addWidget(m_videoHint, 0, Qt::AlignHCenter);
    videoLayout->addWidget(m_openExternal, 0, Qt::AlignHCenter);
    videoLayout->addStretch(1);
    connect(m_openExternal, &QPushButton::clicked, this, [this] {
        if (m_index >= 0 && m_index < m_items.size()) {
            emit openExternalRequested(m_items.at(m_index).mediaId);
        }
    });

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_canvas);
    m_stack->addWidget(m_videoPanel);

    m_previous = viewerButton(this, QStringLiteral("mediaViewerPrevious"),
                              QStringLiteral("chevron-left"), tr("Previous"));
    m_next = viewerButton(this, QStringLiteral("mediaViewerNext"),
                          QStringLiteral("chevron-right"), tr("Next"));
    m_previous->setFixedSize(48, 48);
    m_next->setFixedSize(48, 48);
    m_previous->setIconSize(QSize(24, 24));
    m_next->setIconSize(QSize(24, 24));
    connect(m_previous, &QToolButton::clicked, this, &MediaViewer::previous);
    connect(m_next, &QToolButton::clicked, this, &MediaViewer::next);

    auto *center = new QHBoxLayout();
    center->setContentsMargins(8, 0, 8, 0);
    center->setSpacing(4);
    center->addWidget(m_previous, 0, Qt::AlignVCenter);
    center->addWidget(m_stack, 1);
    center->addWidget(m_next, 0, Qt::AlignVCenter);

    m_save = viewerButton(this, QStringLiteral("mediaViewerSave"),
                          QStringLiteral("download"), tr("Save"));
    m_showInChat = viewerButton(this, QStringLiteral("mediaViewerShowInChat"),
                                QStringLiteral("chats"), tr("Show in chat"));
    m_delete = viewerButton(this, QStringLiteral("mediaViewerDelete"),
                            QStringLiteral("trash"), tr("Delete"));
    connect(m_save, &QToolButton::clicked, this, [this] {
        if (m_index >= 0 && m_index < m_items.size()) {
            emit saveRequested(m_items.at(m_index).mediaId);
        }
    });
    connect(m_showInChat, &QToolButton::clicked, this, [this] {
        if (m_index >= 0 && m_index < m_items.size()) {
            emit showInChatRequested(m_items.at(m_index).mediaId);
            close();
        }
    });
    connect(m_delete, &QToolButton::clicked, this, [this] {
        if (m_index >= 0 && m_index < m_items.size()) {
            emit deleteRequested(m_items.at(m_index).mediaId);
        }
    });

    auto *bottomBar = new QHBoxLayout();
    bottomBar->setContentsMargins(20, 8, 20, 16);
    bottomBar->setSpacing(10);
    bottomBar->addStretch(1);
    bottomBar->addWidget(m_save);
    bottomBar->addWidget(m_showInChat);
    bottomBar->addWidget(m_delete);
    bottomBar->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(topBar);
    layout->addLayout(center, 1);
    layout->addLayout(bottomBar);

    updateChrome();
}

void MediaViewer::setItems(const QVector<MediaViewerItem> &items, int index)
{
    m_items = items;
    m_index = m_items.isEmpty() ? 0 : qBound(0, index, m_items.size() - 1);
    showCurrent();
}

void MediaViewer::showCurrent()
{
    updateChrome();
}

void MediaViewer::next()
{
    if (m_items.size() < 2) {
        return;
    }
    m_index = (m_index + 1) % m_items.size();
    updateChrome();
}

void MediaViewer::previous()
{
    if (m_items.size() < 2) {
        return;
    }
    m_index = (m_index - 1 + m_items.size()) % m_items.size();
    updateChrome();
}

double MediaViewer::zoom() const
{
    return m_canvas->zoom();
}

void MediaViewer::updateChrome()
{
    const bool hasItems = m_index >= 0 && m_index < m_items.size();
    m_previous->setVisible(m_items.size() > 1);
    m_next->setVisible(m_items.size() > 1);
    m_save->setEnabled(hasItems);
    m_delete->setEnabled(hasItems);
    m_showInChat->setEnabled(hasItems);

    if (!hasItems) {
        m_title->setText(tr("No media"));
        m_counter->clear();
        m_canvas->setImage({});
        m_stack->setCurrentWidget(m_canvas);
        return;
    }

    const MediaViewerItem &item = m_items.at(m_index);
    m_title->setText(item.title.isEmpty() ? item.mediaId : item.title);
    m_counter->setText(m_items.size() > 1
                           ? QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_items.size())
                           : QString());

    if (item.kind == MediaKind::Video) {
        m_stack->setCurrentWidget(m_videoPanel);
        m_videoHint->setText(tr("Video: %1\n%2")
                                 .arg(item.title.isEmpty() ? item.mediaId : item.title,
                                      item.subtitle));
        m_openExternal->setEnabled(!item.path.isEmpty());
    } else {
        m_stack->setCurrentWidget(m_canvas);
        QPixmap image = item.image;
        if (image.isNull() && !item.path.isEmpty()) {
            image.load(item.path);
        }
        m_canvas->setImage(image);
    }
}

void MediaViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_Left:
        previous();
        return;
    case Qt::Key_Right:
        next();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_canvas->setZoom(m_canvas->zoom() * 1.15);
        return;
    case Qt::Key_Minus:
        m_canvas->setZoom(m_canvas->zoom() / 1.15);
        return;
    case Qt::Key_0:
        m_canvas->resetView();
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(event);
}

void MediaViewer::closeEvent(QCloseEvent *event)
{
    emit closed();
    QDialog::closeEvent(event);
}

} // namespace mini
