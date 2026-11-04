#include "ui/image_view.h"
#include <QKeyEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent), m_scene(new QGraphicsScene(this)),
      m_pixmapItem(new QGraphicsPixmapItem())
{
    setScene(m_scene);
    m_scene->addItem(m_pixmapItem);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor("#EDF2F7"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QPointF ImageView::imagePosition(const QPoint& pos) const
{
    return m_pixmapItem->mapFromScene(mapToScene(pos));
}

QPointF ImageView::boundedImagePosition(const QPoint& pos) const
{
    const QPointF point = imagePosition(pos);
    const QSize size = getImageSize();
    return {std::clamp(point.x(), 0.0, double(size.width())),
            std::clamp(point.y(), 0.0, double(size.height()))};
}

bool ImageView::containsImagePosition(const QPointF& point) const
{
    const auto size = getImageSize();
    return !size.isEmpty() && point.x() >= 0 && point.y() >= 0
        && point.x() <= size.width() && point.y() <= size.height();
}

void ImageView::mousePressEvent(QMouseEvent* event)
{
    if (!m_zoomEnabled || getImageSize().isEmpty()) {
        event->accept();
        return;
    }
    if (m_drawingMode == DrawingMode::None) {
        if (event->button() == Qt::RightButton) showContextMenu(event->pos());
        else QGraphicsView::mousePressEvent(event);
        return;
    }
    event->accept();
    if (event->button() == Qt::RightButton) {
        if (m_drawingMode == DrawingMode::Rectangle
            || (m_drawingMode == DrawingMode::Roi && !m_roiReady)
            || (m_drawingMode == DrawingMode::ReferenceLine && !m_referenceEnd))
            cancelDrawing();
        else confirmDrawing();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    const QPointF point = imagePosition(event->pos());
    if (!containsImagePosition(point)) return;

    switch (m_drawingMode) {
    case DrawingMode::Roi:
        if (m_roiReady) {
            m_roiHandle = getRoiHandleAtPos(point);
            m_dragStartPos = point;
            m_dragStartRect = m_rectangle->rect();
            setCursorForHandle(m_roiHandle);
            break;
        }
        [[fallthrough]];
    case DrawingMode::Rectangle: {
        m_rectangleStart = point;
        delete m_rectangle;
        m_rectangle = new QGraphicsRectItem(QRectF(point, point), m_pixmapItem);
        QPen pen(m_drawingMode == DrawingMode::Roi ? Qt::green : Qt::red, 2);
        pen.setCosmetic(true);
        m_rectangle->setPen(pen);
        break;
    }
    case DrawingMode::Polygon:
        if (m_polygonPoints.isEmpty() || m_polygonPoints.last() != point) {
            m_polygonPoints.append(point);
            updatePolygonPath();
            const auto type = m_drawingType;
            emit drawingStateChanged();
            emit polygonDrawingPointAdded(type, point);
        }
        break;
    case DrawingMode::ReferenceLine:
        if (!m_referenceStart) m_referenceStart = point;
        else if (QLineF(*m_referenceStart, point).length() >= 2.0) {
            m_referenceEnd = point;
            updateReferencePreview(point);
        }
        emit drawingStateChanged();
        break;
    case DrawingMode::None:
        break;
    }
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_zoomEnabled || getImageSize().isEmpty()) {
        event->accept();
        return;
    }
    const QPointF point = boundedImagePosition(event->pos());
    if ((event->buttons() & Qt::LeftButton) && m_rectangle) {
        updateRectangle(point);
        event->accept();
        return;
    }
    if (m_drawingMode == DrawingMode::Roi && m_roiReady)
        setCursorForHandle(getRoiHandleAtPos(imagePosition(event->pos())));
    if (m_drawingMode == DrawingMode::ReferenceLine && m_referenceStart && !m_referenceEnd)
        updateReferencePreview(point);

    if (event->modifiers() & Qt::ControlModifier) {
        // Pixel sampling uses the actual position, not the clamped drag endpoint.
        const QPointF raw = imagePosition(event->pos());
        const int x = int(std::floor(raw.x()));
        const int y = int(std::floor(raw.y()));
        const auto size = getImageSize();
        if (x >= 0 && y >= 0 && x < size.width() && y < size.height()) {
            const QColor color = m_pixmapItem->pixmap().toImage().pixelColor(x, y);
            emit pixelInfoChanged(x, y, color, qGray(color.rgb()));
        }
    }
    if (m_drawingMode == DrawingMode::None) QGraphicsView::mouseMoveEvent(event);
    else event->accept();
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_zoomEnabled) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_rectangle
        && (m_drawingMode == DrawingMode::Roi || m_drawingMode == DrawingMode::Rectangle)) {
        updateRectangle(boundedImagePosition(event->pos()));
        m_roiHandle = None;
        const auto rect = m_rectangle->rect();
        if (rect.width() < 3 || rect.height() < 3) {
            delete m_rectangle;
            m_rectangle = nullptr;
            m_roiReady = false;
        } else if (m_drawingMode == DrawingMode::Roi) {
            m_roiReady = true;
        } else {
            confirmDrawing();
        }
        emit drawingStateChanged();
        event->accept();
        return;
    }
    if (m_drawingMode == DrawingMode::None) QGraphicsView::mouseReleaseEvent(event);
    else event->accept();
}

void ImageView::keyPressEvent(QKeyEvent* event)
{
    if (m_drawingMode != DrawingMode::None) {
        switch (event->key()) {
        case Qt::Key_Escape:
            cancelDrawing();
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            confirmDrawing();
            event->accept();
            return;
        case Qt::Key_Backspace:
            if (m_drawingMode == DrawingMode::Polygon && !m_polygonPoints.isEmpty()) {
                m_polygonPoints.removeLast();
                updatePolygonPath();
                emit drawingStateChanged();
            }
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
