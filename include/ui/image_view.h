#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <optional>
#include <opencv2/core/types.hpp>

class ImageView : public QGraphicsView
{
    Q_OBJECT
public:
    enum class DrawingMode { None, Roi, Polygon, Rectangle, ReferenceLine };
    Q_ENUM(DrawingMode)

    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    // Same-size preview updates preserve the viewport and an active drawing.
    void setImageKeepZoom(const QImage& image);
    void clear();

    void setRoiMode(bool enabled);
    void clearRoi();
    void finishRoiMode();
    void startPolygonDrawing(const QString& type);
    void clearPolygonDrawing();
    void clearPolygon();
    QVector<QPointF> getPolygonPoints() const { return m_polygonPoints; }
    void startRectangleDrawing(const QString& type);
    void clearRectangleDrawing();
    void startReferenceLineDrawing();
    void clearReferenceLine();

    DrawingMode drawingMode() const { return m_drawingMode; }
    QString drawingHint() const;
    void cancelDrawing();
    void confirmDrawing();

    void resetZoom();
    void zoomIn();
    void zoomOut();
    void zoomActualSize();
    double zoomFactor() const { return transform().m11(); }
    bool isFitToWindow() const { return m_fitToWindow; }
    QSize imageSize() const { return getImageSize(); }
    void setZoomEnabled(bool enabled);
    bool isZoomEnabled() const { return m_zoomEnabled; }
    void showContextMenu(const QPoint& viewportPos);

signals:
    void viewChanged();
    void drawingStateChanged();
    void pixelInfoChanged(int x, int y, const QColor& rgb, int gray);
    void roiSelected(const QRectF& rect);
    void polygonDrawingPointAdded(const QString& type, const QPointF& point);
    void polygonDrawingFinished(const QString& type, QVector<QPointF> points);
    void referenceLineDrawn(const cv::Point2f& start, const cv::Point2f& end);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum RoiHandle { None, Move, TopLeft, TopRight, BottomLeft, BottomRight, Top, Bottom, Left, Right };
    bool beginDrawing(DrawingMode mode, const QString& type = {});
    void endDrawing();
    void clearDrawingOverlays();
    void updatePolygonPath(bool closed = false);
    void updateReferencePreview(const QPointF& point);
    void updateRectangle(const QPointF& point);
    RoiHandle getRoiHandleAtPos(const QPointF& point) const;
    void setCursorForHandle(RoiHandle handle);
    QPointF imagePosition(const QPoint& viewportPos) const;
    QPointF boundedImagePosition(const QPoint& viewportPos) const;
    bool containsImagePosition(const QPointF& point) const;
    void setImageInternal(const QImage& image, bool keepZoom);
    QSize getImageSize() const { return m_pixmapItem->pixmap().size(); }
    void fitImage();
    void setZoomFactor(double factor);

    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    bool m_fitToWindow = true;
    bool m_zoomEnabled = true;

    DrawingMode m_drawingMode = DrawingMode::None;
    QString m_drawingType;
    // Only drafts belong to the active tool; a confirmed reference line can survive cancellation.
    QGraphicsRectItem* m_rectangle = nullptr;
    QPointF m_rectangleStart;
    bool m_roiReady = false;
    RoiHandle m_roiHandle = None;
    QPointF m_dragStartPos;
    QRectF m_dragStartRect;
    QVector<QPointF> m_polygonPoints;
    QGraphicsPathItem* m_polygonPath = nullptr;
    std::optional<QPointF> m_referenceStart;
    std::optional<QPointF> m_referenceEnd;
    QGraphicsLineItem* m_referencePreview = nullptr;
    QGraphicsLineItem* m_referenceLine = nullptr;
};
