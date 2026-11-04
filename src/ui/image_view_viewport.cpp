#include "ui/image_view.h"
#include <QAction>
#include <QResizeEvent>
#include <QMenu>
#include <QPainter>
#include <algorithm>
#include <cmath>

void ImageView::setImageInternal(const QImage& image, bool keepZoom)
{
    if (image.isNull()) return;
    const bool sameSize = getImageSize() == image.size();
    m_pixmapItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    if (!keepZoom || !sameSize) clearDrawingOverlays();
    // A different image coordinate space needs a new fit, even for live results.
    if (!keepZoom || !sameSize) resetZoom();
    else if (m_fitToWindow) fitImage();
    emit viewChanged();
}

void ImageView::setImage(const QImage& image) { setImageInternal(image, false); }
void ImageView::setImageKeepZoom(const QImage& image) { setImageInternal(image, true); }

void ImageView::fitImage()
{
    const auto size = getImageSize();
    if (size.isEmpty() || viewport()->width() <= 4 || viewport()->height() <= 4) return;
    const double factor = std::min((viewport()->width() - 4.0) / size.width(),
                                   (viewport()->height() - 4.0) / size.height());
    setTransform(QTransform::fromScale(factor, factor));
    centerOn(m_pixmapItem);
}

void ImageView::resetZoom()
{
    m_fitToWindow = true;
    // No scrollbar feedback loop while fitting in resizeEvent.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fitImage();
    emit viewChanged();
}

void ImageView::setZoomFactor(double factor)
{
    if (!m_zoomEnabled || getImageSize().isEmpty() || !std::isfinite(factor)) return;
    m_fitToWindow = false;
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    factor = std::clamp(factor, 0.01, 64.0);
    const double relative = factor / zoomFactor();
    scale(relative, relative);
    emit viewChanged();
}

void ImageView::zoomIn() { setZoomFactor(zoomFactor() * 1.25); }
void ImageView::zoomOut() { setZoomFactor(zoomFactor() / 1.25); }
void ImageView::zoomActualSize() { setZoomFactor(1.0); }

void ImageView::setZoomEnabled(bool enabled)
{
    if (!enabled) cancelDrawing();
    m_zoomEnabled = enabled;
    emit viewChanged();
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0) setZoomFactor(zoomFactor() * std::pow(1.25, delta / 120.0));
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitToWindow) fitImage();
    emit viewChanged();
}

void ImageView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    if (!getImageSize().isEmpty()) return;
    QPainter painter(viewport());
    painter.setPen(QColor("#52657A"));
    painter.drawText(viewport()->rect().adjusted(20, 20, -20, -20),
        Qt::AlignCenter | Qt::TextWordWrap,
        tr("尚未加载图片\n\n点击左侧“添加图片”或“导入文件夹”开始检测"));
}

void ImageView::clear()
{
    clearDrawingOverlays();
    m_pixmapItem->setPixmap({});
    m_scene->setSceneRect({});
    resetTransform();
    resetZoom();
    viewport()->update();
}

void ImageView::showContextMenu(const QPoint& viewportPos)
{
    QMenu menu(this);
    auto* fit = menu.addAction(tr("适应窗口"), this, &ImageView::resetZoom);
    auto* actual = menu.addAction(tr("100% 显示"), this, &ImageView::zoomActualSize);
    fit->setEnabled(m_zoomEnabled && !getImageSize().isEmpty());
    actual->setEnabled(fit->isEnabled());
    menu.exec(viewport()->mapToGlobal(viewportPos));
}
