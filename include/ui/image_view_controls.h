#pragma once
#include <QWidget>

class ImageView;
class QLabel;
class QToolButton;

// Presentation-only controls: ImageView owns the image and viewport state.
class ImageViewControls : public QWidget
{
public:
    explicit ImageViewControls(ImageView* view, QWidget* parent = nullptr);

private:
    void refresh();
    ImageView* m_view;
    QLabel* m_imageInfo;
    QLabel* m_zoomInfo;
    QToolButton* m_fit;
    QToolButton* m_actual;
    QToolButton* m_zoomIn;
    QToolButton* m_zoomOut;
    QWidget* m_drawingRow;
    QLabel* m_drawingHint;
};
