#include "enhance_tab_widget.h"
#include "ui_enhance_tab.h"
#include "config/constants.h"
#include "config/config_manager.h"
#include "ui/slider_spinbox_binder.h"
#include "controllers/roi_ui_controller.h"
#include "image_view.h"
#include "algorithm/image_utils.h"
#include <opencv2/imgproc.hpp>

EnhanceTabWidget::EnhanceTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::EnhanceTabWidget)
    , m_pipeline(pipelineAccess)
{
    m_ui->setupUi(this);
    connect(m_ui->btn_resetBC, &QPushButton::clicked, this, &EnhanceTabWidget::onResetClicked);
    connect(m_ui->btn_saveBC, &QPushButton::clicked, this, &EnhanceTabWidget::onSaveClicked);
    connect(m_ui->btn_undoBC, &QPushButton::clicked, this, &EnhanceTabWidget::onUndoClicked);

    // 设置 slider 和 spinbox 的范围、步长和默认值
    m_ui->Slider_brightness->setRange(-100, 100);
    m_ui->Slider_brightness->setValue(0);
    m_ui->spinBox_brightness->setRange(-100, 100);
    m_ui->spinBox_brightness->setValue(0);
    bindSliderAndSpinBox(m_ui->Slider_brightness, m_ui->spinBox_brightness);

    m_ui->Slider_contrast->setRange(0, 300);
    m_ui->Slider_contrast->setValue(100);
    m_ui->spinBox_contrast->setRange(0, 300);
    m_ui->spinBox_contrast->setValue(100);
    bindSliderAndSpinBox(m_ui->Slider_contrast, m_ui->spinBox_contrast);

    m_ui->Slider_gamma->setRange(10, 300);
    m_ui->Slider_gamma->setValue(100);
    m_ui->spinBox_gamma->setRange(10, 300);
    m_ui->spinBox_gamma->setValue(100);
    bindSliderAndSpinBox(m_ui->Slider_gamma, m_ui->spinBox_gamma);

    m_ui->Slider_sharpen->setRange(0, 500);
    m_ui->Slider_sharpen->setValue(100);
    m_ui->spinBox_sharpen->setRange(0, 500);
    m_ui->spinBox_sharpen->setValue(100);
    bindSliderAndSpinBox(m_ui->Slider_sharpen, m_ui->spinBox_sharpen);
    
    // CLAHE 控件初始化
    m_ui->Slider_claheClip->setRange(0, 100);
    m_ui->Slider_claheClip->setValue(40);
    m_ui->Slider_claheGrid->setRange(4, 32);
    m_ui->Slider_claheGrid->setValue(8);

    // 所有 slider 变化统一走 onSliderChanged
    connect(m_ui->Slider_brightness, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->Slider_contrast, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->Slider_gamma, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->Slider_sharpen, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->Slider_claheClip, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->Slider_claheGrid, &QSlider::valueChanged, this, &EnhanceTabWidget::onSliderChanged);
    connect(m_ui->checkBox_clahe, &QCheckBox::toggled, this, &EnhanceTabWidget::onSliderChanged);

    m_enhancementHistory.push(captureState());
    updateUndoUi();
}

void EnhanceTabWidget::onSliderChanged()
{
    syncConfigToPipeline();
    updateUndoUi();
    previewEnhance();
}

void EnhanceTabWidget::onResetClicked()
{
    if (!m_pipeline) return;
    EnhancementState defaults;
    applyState(defaults);
    updateUndoUi();

}

void EnhanceTabWidget::onSaveClicked()
{
    EnhancementState current = captureState();
    pushSnapshot(current);
    syncConfigToPipeline();
}

void EnhanceTabWidget::onUndoClicked()
{
    if (m_enhancementHistory.isEmpty())
        return;

    EnhancementState current = captureState();
    const EnhancementState& latest = m_enhancementHistory.top();

    // 先撤销未保存的改动
    if (!(current == latest)) {
        applyState(latest);
        return;
    }

    // 继续回溯到更早快照
    if (m_enhancementHistory.size() > 1) {
        m_enhancementHistory.pop();
        applyState(m_enhancementHistory.top());
    }
    updateUndoUi();
}

EnhanceTabWidget::EnhancementState EnhanceTabWidget::captureState() const
{
    EnhancementState state;
    state.brightness = m_ui->Slider_brightness->value();
    state.contrast   = m_ui->Slider_contrast->value();
    state.gamma      = m_ui->Slider_gamma->value();
    state.sharpen    = m_ui->Slider_sharpen->value();
    state.claheEnabled = m_ui->checkBox_clahe->isChecked();
    state.claheClipLimit = m_ui->Slider_claheClip->value();
    state.claheGridSize = m_ui->Slider_claheGrid->value();
    return state;
}

void EnhanceTabWidget::applyState(const EnhancementState &state)
{
    applyStateQuiet(state);
    syncConfigToPipeline();
    updateUndoUi();
    emit processRequested();

}

void EnhanceTabWidget::pushSnapshot(const EnhancementState &state)
{
    if (m_enhancementHistory.isEmpty() || !(state == m_enhancementHistory.top())) 
    {
        m_enhancementHistory.push(state);
        updateUndoUi();
    }
}

void EnhanceTabWidget::updateUndoUi()
{
    bool hasSnapshot = !m_enhancementHistory.isEmpty();
    bool canStepBack = m_enhancementHistory.size() > 1
        || (hasSnapshot && !(captureState() == m_enhancementHistory.top()));
    m_ui->btn_undoBC->setEnabled(hasSnapshot && canStepBack);
}

void EnhanceTabWidget::syncConfigToPipeline()
{
    EnhancementState current = captureState();
    PipelineConfig cfg = m_pipeline->getConfigSnapshot();
    cfg.enhance.brightness = current.brightness;
    cfg.enhance.contrast = current.contrast;
    cfg.enhance.gamma = current.gamma;
    cfg.enhance.sharpen = current.sharpen;
    cfg.enhance.claheEnabled = current.claheEnabled;
    cfg.enhance.claheClipLimit = current.claheClipLimit;
    cfg.enhance.claheGridSize = current.claheGridSize;
    m_pipeline->setConfig(cfg);
}

void EnhanceTabWidget::previewEnhance()
{
    if (!m_view || !m_roiManager) return;

    // 获取当前 ROI 图像（与 Pipeline 使用相同的基图）
    cv::Mat base = m_roiManager->getCurrentImage();
    if (base.empty()) return;

    // 获取当前增强参数
    int brightness = m_ui->Slider_brightness->value();
    int contrast   = m_ui->Slider_contrast->value();
    int gamma      = m_ui->Slider_gamma->value();
    int sharpen    = m_ui->Slider_sharpen->value();

    // 轻量增强处理：亮度/对比度 + Gamma + 锐化（与 Pipeline adjustParameter 保持一致）
    cv::Mat result = base.clone();

    // 亮度 + 对比度
    result.convertTo(result, -1, contrast / 100.0, brightness);

    // Gamma 校正（与 Pipeline adjustParameter 保持一致）
    double g = gamma / 100.0;
    cv::Mat lookUpLUT(1, 256, CV_8U);
    uchar* p = lookUpLUT.ptr();
    for (int i = 0; i < 256; ++i) {
        p[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, g) * 255.0);
    }
    cv::LUT(result, lookUpLUT, result);

    // 锐化（USM，与 ImageProcessor::adjustParameter 一致）
    double sharpenVal = sharpen / 100.0;
    if (sharpenVal > 0.0) {
        cv::Mat blurred;
        cv::GaussianBlur(result, blurred, cv::Size(0, 0), 1.0);
        cv::addWeighted(result, 1.0 + sharpenVal, blurred, -sharpenVal, 0, result);
    }
    
    // CLAHE 处理
    if (m_ui->checkBox_clahe->isChecked()) {
        double clipLimit = m_ui->Slider_claheClip->value() / 20.0;
        int gridSize = m_ui->Slider_claheGrid->value();
        
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(gridSize, gridSize));
        cv::Mat lab;
        cv::cvtColor(result, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> labChannels;
        cv::split(lab, labChannels);
        clahe->apply(labChannels[0], labChannels[0]);
        cv::merge(labChannels, lab);
        cv::cvtColor(lab, result, cv::COLOR_Lab2BGR);
    }

    m_view->setImage(ImageUtils::matToQImage(result));
}

EnhanceTabWidget::~EnhanceTabWidget()
{
    delete m_ui;
}

void EnhanceTabWidget::getEnhanceConfig(int& brightness, int& contrast, int& gamma, int& sharpen,
                                         bool& claheEnabled, int& claheClipLimit, int& claheGridSize) const
{
    EnhancementState state = captureState();
    brightness = state.brightness;
    contrast = state.contrast;
    gamma = state.gamma;
    sharpen = state.sharpen;
    claheEnabled = state.claheEnabled;
    claheClipLimit = state.claheClipLimit;
    claheGridSize = state.claheGridSize;
}

void EnhanceTabWidget::setEnhanceConfig(int brightness, int contrast, int gamma, int sharpen,
                                         bool claheEnabled, int claheClipLimit, int claheGridSize)
{
    EnhancementState state;
    state.brightness = brightness;
    state.contrast = contrast;
    state.gamma = gamma;
    state.sharpen = sharpen;
    state.claheEnabled = claheEnabled;
    state.claheClipLimit = claheClipLimit;
    state.claheGridSize = claheGridSize;
    applyStateQuiet(state);
    m_enhancementHistory.clear();
    m_enhancementHistory.push(captureState());
    updateUndoUi();
}

void EnhanceTabWidget::applyStateQuiet(const EnhancementState &state)
{
    // 阻断所有slider和spinbox信号，防止级联触发processRequested
    const QSignalBlocker b1(m_ui->Slider_brightness);
    const QSignalBlocker b2(m_ui->Slider_contrast);
    const QSignalBlocker b3(m_ui->Slider_gamma);
    const QSignalBlocker b4(m_ui->Slider_sharpen);
    const QSignalBlocker b5(m_ui->Slider_claheClip);
    const QSignalBlocker b6(m_ui->Slider_claheGrid);
    const QSignalBlocker cb1(m_ui->checkBox_clahe);
    const QSignalBlocker sb1(m_ui->spinBox_brightness);
    const QSignalBlocker sb2(m_ui->spinBox_contrast);
    const QSignalBlocker sb3(m_ui->spinBox_gamma);
    const QSignalBlocker sb4(m_ui->spinBox_sharpen);

    m_ui->Slider_brightness->setValue(state.brightness);
    m_ui->Slider_contrast->setValue(state.contrast);
    m_ui->Slider_gamma->setValue(state.gamma);
    m_ui->Slider_sharpen->setValue(state.sharpen);
    m_ui->checkBox_clahe->setChecked(state.claheEnabled);
    m_ui->Slider_claheClip->setValue(state.claheClipLimit);
    m_ui->Slider_claheGrid->setValue(state.claheGridSize);

    m_ui->spinBox_brightness->setValue(state.brightness);
    m_ui->spinBox_contrast->setValue(state.contrast);
    m_ui->spinBox_gamma->setValue(state.gamma);
    m_ui->spinBox_sharpen->setValue(state.sharpen);


}

void EnhanceTabWidget::connectSignals(const SignalContext& ctx,
                                      std::function<void()> onExecutePipeline,
                                      std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    Q_UNUSED(onConfigSaved);
    m_view = ctx.view;
    m_roiManager = ctx.roiManager;
    connect(this, &EnhanceTabWidget::processRequested,
            this, [onExecutePipeline]() { onExecutePipeline(); });
}

// ========== IConfigurableTab 接口实现 ==========

void EnhanceTabWidget::saveToConfig(PipelineConfig& config) const
{
    config.enhance.brightness = m_ui->Slider_brightness->value();
    config.enhance.contrast   = m_ui->Slider_contrast->value();
    config.enhance.gamma      = m_ui->Slider_gamma->value();
    config.enhance.sharpen    = m_ui->Slider_sharpen->value();
    config.enhance.claheEnabled = m_ui->checkBox_clahe->isChecked();
    config.enhance.claheClipLimit = m_ui->Slider_claheClip->value();
    config.enhance.claheGridSize = m_ui->Slider_claheGrid->value();
}

void EnhanceTabWidget::loadFromConfig(const PipelineConfig& config)
{
    setEnhanceConfig(
        config.enhance.brightness,
        config.enhance.contrast,
        config.enhance.gamma,
        config.enhance.sharpen,
        config.enhance.claheEnabled,
        config.enhance.claheClipLimit,
        config.enhance.claheGridSize
    );
}
