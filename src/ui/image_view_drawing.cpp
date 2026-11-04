#include "ui/image_view.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
QPen overlayPen(const QColor& color, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, 2, style);
    pen.setCosmetic(true);
    return pen;
}
}

bool ImageView::beginDrawing(DrawingMode mode, const QString& type)
{
    if (!m_zoomEnabled || getImageSize().isEmpty()) return false;
    cancelDrawing();
    clearPolygonDrawing();
    m_drawingMode = mode;
    m_drawingType = type;
    setDragMode(QGraphicsView::NoDrag);
    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
    emit drawingStateChanged();
    return true;
}

void ImageView::endDrawing()
{
    m_drawingMode = DrawingMode::None;
    m_drawingType.clear();
    m_roiReady = false;
    m_roiHandle = None;
    m_referenceStart.reset();
    m_referenceEnd.reset();
    setDragMode(QGraphicsView::ScrollHandDrag);
    setCursor(Qt::ArrowCursor);
    emit drawingStateChanged();
}

void ImageView::cancelDrawing()
{
    delete m_rectangle;
    m_rectangle = nullptr;
    delete m_referencePreview;
    m_referencePreview = nullptr;
    if (m_drawingMode == DrawingMode::Polygon) {
        delete m_polygonPath;
        m_polygonPath = nullptr;
        m_polygonPoints.clear();
    }
    endDrawing();
}

void ImageView::clearDrawingOverlays()
{
    cancelDrawing();
    clearPolygonDrawing();
    delete m_referenceLine;
    m_referenceLine = nullptr;
}

void ImageView::setRoiMode(bool enabled)
{
    if (enabled) beginDrawing(DrawingMode::Roi);
    else clearRoi();
}

void ImageView::clearRoi()
{
    if (m_drawingMode == DrawingMode::Roi) cancelDrawing();
}
void ImageView::finishRoiMode() { clearRoi(); }

void ImageView::startPolygonDrawing(const QString& type) { beginDrawing(DrawingMode::Polygon, type); }
void ImageView::startRectangleDrawing(const QString& type) { beginDrawing(DrawingMode::Rectangle, type); }
void ImageView::startReferenceLineDrawing() { beginDrawing(DrawingMode::ReferenceLine); }
void ImageView::clearPolygon() { clearPolygonDrawing(); }

void ImageView::clearPolygonDrawing()
{
    if (m_drawingMode == DrawingMode::Polygon) cancelDrawing();
    delete m_polygonPath;
    m_polygonPath = nullptr;
    m_polygonPoints.clear();
}

void ImageView::clearRectangleDrawing()
{
    if (m_drawingMode == DrawingMode::Rectangle) cancelDrawing();
}

void ImageView::clearReferenceLine()
{
    if (m_drawingMode == DrawingMode::ReferenceLine) cancelDrawing();
    delete m_referenceLine;
    m_referenceLine = nullptr;
}

QString ImageView::drawingHint() const
{
    switch (m_drawingMode) {
    case DrawingMode::Roi:
        return m_roiReady ? tr("ROI：拖动框或边缘调整；右键 / Enter 确认，Esc 取消")
                          : tr("ROI：按住左键框选；Esc 取消");
    case DrawingMode::Rectangle:
        return tr("区域：按住左键框选，松开确认；Esc 取消");
    case DrawingMode::Polygon:
        return tr("多边形：已选 %1 点（至少 3 个非共线点）；右键 / Enter 确认，Backspace 撤回，Esc 取消").arg(m_polygonPoints.size());
    case DrawingMode::ReferenceLine:
        if (!m_referenceStart) return tr("参考线：左键选择起点；Esc 取消");
        if (!m_referenceEnd) return tr("参考线：左键选择终点（至少 2 像素）；Esc 取消");
        return tr("参考线：右键 / Enter 确认，左键调整终点；Esc 取消");
    case DrawingMode::None:
        return {};
    }
    return {};
}

void ImageView::confirmDrawing()
{
    if (!m_zoomEnabled) return;
    // Snapshot the payload and exit before signals: a receiver may replace the image
    // or start the next tool synchronously.
    if (m_drawingMode == DrawingMode::Roi && m_roiReady && m_rectangle) {
        const QRectF rect = m_rectangle->rect();
        cancelDrawing();
        emit roiSelected(rect);
    } else if (m_drawingMode == DrawingMode::Rectangle && m_rectangle) {
        const QRectF rect = m_rectangle->rect();
        if (rect.width() < 3 || rect.height() < 3) return;
        const QString type = m_drawingType;
        cancelDrawing();
        emit polygonDrawingFinished(type, {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()});
    } else if (m_drawingMode == DrawingMode::Polygon && m_polygonPoints.size() >= 3) {
        double twiceArea = 0;
        for (int i = 0; i < m_polygonPoints.size(); ++i) {
            const auto a = m_polygonPoints[i];
            const auto b = m_polygonPoints[(i + 1) % m_polygonPoints.size()];
            twiceArea += a.x() * b.y() - b.x() * a.y();
        }
        if (std::abs(twiceArea) < 1.0) return;
        const auto points = m_polygonPoints;
        const auto type = m_drawingType;
        updatePolygonPath(true);
        endDrawing(); // Retain the confirmed polygon for template creation.
        emit polygonDrawingFinished(type, points);
    } else if (m_drawingMode == DrawingMode::ReferenceLine && m_referenceStart && m_referenceEnd) {
        const QPointF start = *m_referenceStart, end = *m_referenceEnd;
        delete m_referenceLine;
        m_referenceLine = new QGraphicsLineItem(QLineF(start, end), m_pixmapItem);
        m_referenceLine->setPen(overlayPen(Qt::green));
        cancelDrawing();
        emit referenceLineDrawn(cv::Point2f(start.x(), start.y()), cv::Point2f(end.x(), end.y()));
    }
}

void ImageView::updatePolygonPath(bool closed)
{
    if (!m_polygonPath) {
        m_polygonPath = new QGraphicsPathItem(m_pixmapItem);
        m_polygonPath->setPen(overlayPen(m_drawingType == "template" ? Qt::blue : Qt::red));
    }
    QPainterPath path;
    if (!m_polygonPoints.isEmpty()) {
        path.moveTo(m_polygonPoints.first());
        for (int i = 1; i < m_polygonPoints.size(); ++i) path.lineTo(m_polygonPoints[i]);
        if (closed) path.closeSubpath();
    }
    m_polygonPath->setPath(path);
}

void ImageView::updateReferencePreview(const QPointF& point)
{
    if (!m_referencePreview) {
        m_referencePreview = new QGraphicsLineItem(m_pixmapItem);
        m_referencePreview->setPen(overlayPen(QColor("#D48A00"), Qt::DashLine));
    }
    m_referencePreview->setLine(QLineF(*m_referenceStart, point));
}

void ImageView::updateRectangle(const QPointF& point)
{
    if (!m_roiReady) {
        m_rectangle->setRect(QRectF(m_rectangleStart, point).normalized());
        return;
    }
    if (m_roiHandle == None) return;
    const QSize size = getImageSize();
    const QPointF delta = point - m_dragStartPos;
    QRectF rect = m_dragStartRect;
    constexpr double minSize = 3.0;
    if (m_roiHandle == Move) {
        rect.moveLeft(std::clamp(rect.left() + delta.x(), 0.0, size.width() - rect.width()));
        rect.moveTop(std::clamp(rect.top() + delta.y(), 0.0, size.height() - rect.height()));
    } else {
        if (m_roiHandle == Left || m_roiHandle == TopLeft || m_roiHandle == BottomLeft)
            rect.setLeft(std::clamp(rect.left() + delta.x(), 0.0, rect.right() - minSize));
        if (m_roiHandle == Right || m_roiHandle == TopRight || m_roiHandle == BottomRight)
            rect.setRight(std::clamp(rect.right() + delta.x(), rect.left() + minSize, double(size.width())));
        if (m_roiHandle == Top || m_roiHandle == TopLeft || m_roiHandle == TopRight)
            rect.setTop(std::clamp(rect.top() + delta.y(), 0.0, rect.bottom() - minSize));
        if (m_roiHandle == Bottom || m_roiHandle == BottomLeft || m_roiHandle == BottomRight)
            rect.setBottom(std::clamp(rect.bottom() + delta.y(), rect.top() + minSize, double(size.height())));
    }
    m_rectangle->setRect(rect);
}

ImageView::RoiHandle ImageView::getRoiHandleAtPos(const QPointF& point) const
{
    if (!m_roiReady || !m_rectangle) return None;
    const auto rect = m_rectangle->rect();
    // Keep the center draggable even for small ROIs at low zoom.
    const double tolerance = std::min({8.0 / zoomFactor(), rect.width() / 3, rect.height() / 3});
    if (!rect.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) return None;
    const bool left = std::abs(point.x() - rect.left()) <= tolerance;
    const bool right = std::abs(point.x() - rect.right()) <= tolerance;
    const bool top = std::abs(point.y() - rect.top()) <= tolerance;
    const bool bottom = std::abs(point.y() - rect.bottom()) <= tolerance;
    if (top && left) return TopLeft;
    if (top && right) return TopRight;
    if (bottom && left) return BottomLeft;
    if (bottom && right) return BottomRight;
    if (left) return Left;
    if (right) return Right;
    if (top) return Top;
    if (bottom) return Bottom;
    return rect.contains(point) ? Move : None;
}

void ImageView::setCursorForHandle(RoiHandle handle)
{
    switch (handle) {
    case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
    case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
    case Left: case Right: setCursor(Qt::SizeHorCursor); break;
    case Move: setCursor(Qt::OpenHandCursor); break;
    default: setCursor(Qt::CrossCursor); break;
    }
}
