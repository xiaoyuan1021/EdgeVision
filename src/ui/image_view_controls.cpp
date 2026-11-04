#include "ui/image_view_controls.h"
#include "ui/image_view.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>

ImageViewControls::ImageViewControls(ImageView* view, QWidget* parent)
    : QWidget(parent), m_view(view)
{
    setObjectName("imageViewControls");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* layout = new QHBoxLayout();
    root->addLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const auto button = [this, layout](const QString& text, const QString& name,
                                     const QString& hint, auto slot) {
        auto* control = new QToolButton(this);
        control->setText(text);
        control->setObjectName(name);
        control->setToolTip(hint);
        control->setAccessibleName(hint);
        layout->addWidget(control);
        connect(control, &QToolButton::clicked, m_view, slot);
        return control;
    };
    m_fit = button(tr("适应"), "btn_fitImage", tr("适应窗口（随窗口大小调整）"), &ImageView::resetZoom);
    m_fit->setCheckable(true);
    m_actual = button("100%", "btn_actualSize", tr("按原始像素比例显示"), &ImageView::zoomActualSize);
    m_zoomOut = button("−", "btn_zoomOut", tr("缩小"), &ImageView::zoomOut);
    m_zoomIn = button("+", "btn_zoomIn", tr("放大"), &ImageView::zoomIn);
    m_zoomInfo = new QLabel(this);
    m_zoomInfo->setObjectName("label_zoomInfo");
    layout->addWidget(m_zoomInfo);
    layout->addStretch();
    m_imageInfo = new QLabel(this);
    m_imageInfo->setObjectName("label_imageInfo");
    layout->addWidget(m_imageInfo);
    m_drawingRow = new QWidget(this);
    auto* drawingLayout = new QHBoxLayout(m_drawingRow);
    drawingLayout->setContentsMargins(0, 0, 0, 0);
    m_drawingHint = new QLabel(m_drawingRow);
    m_drawingHint->setObjectName("label_drawingHint");
    m_drawingHint->setWordWrap(true);
    drawingLayout->addWidget(m_drawingHint, 1);
    auto* cancel = new QToolButton(m_drawingRow);
    cancel->setObjectName("btn_cancelDrawing");
    cancel->setText(tr("取消"));
    cancel->setToolTip(tr("取消当前绘制（Esc）"));
    connect(cancel, &QToolButton::clicked, view, &ImageView::cancelDrawing);
    drawingLayout->addWidget(cancel);
    root->addWidget(m_drawingRow);
    connect(view, &ImageView::viewChanged, this, &ImageViewControls::refresh);
    connect(view, &ImageView::drawingStateChanged, this, &ImageViewControls::refresh);
    refresh();
}

void ImageViewControls::refresh()
{
    const auto size = m_view->imageSize();
    const bool enabled = !size.isEmpty() && m_view->isZoomEnabled();
    for (auto* control : {m_fit, m_actual, m_zoomIn, m_zoomOut}) control->setEnabled(enabled);
    m_fit->setChecked(m_view->isFitToWindow());
    m_zoomInfo->setText(size.isEmpty() ? "—" : QString::number(m_view->zoomFactor() * 100, 'f', 0) + "%");
    m_imageInfo->setText(size.isEmpty() ? tr("无图像") : tr("%1 × %2").arg(size.width()).arg(size.height()));
    m_drawingHint->setText(m_view->drawingHint());
    m_drawingRow->setVisible(m_view->drawingMode() != ImageView::DrawingMode::None);
}
