#include "filter_tab_widget.h"
#include "ui_filter_tab.h"
#include "logger.h"
#include "config/constants.h"
#include "config/config_manager.h"
#include "ui/slider_spinbox_binder.h"
#include "ui/scoped_widget_signals.h"

FilterTabWidget::FilterTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::FilterTabWidget)
    , m_pipeline(pipelineAccess)
{
    m_ui->setupUi(this);

    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setMinimumSize(0, 0);
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    // 初始化所有 slider 和 spinbox
    // 灰度
    m_ui->Slider_grayLow->setRange(0, 255);
    m_ui->Slider_grayLow->setValue(0);
    m_ui->Slider_grayHigh->setRange(0, 255);
    m_ui->Slider_grayHigh->setValue(120);

    // RGB
    m_ui->Slider_rgb_R_Low->setRange(0, 255);
    m_ui->Slider_rgb_R_Low->setValue(0);
    m_ui->Slider_rgb_R_High->setRange(0, 255);
    m_ui->Slider_rgb_R_High->setValue(120);
    m_ui->Slider_rgb_G_Low->setRange(0, 255);
    m_ui->Slider_rgb_G_Low->setValue(0);
    m_ui->Slider_rgb_G_High->setRange(0, 255);
    m_ui->Slider_rgb_G_High->setValue(120);
    m_ui->Slider_rgb_B_Low->setRange(0, 255);
    m_ui->Slider_rgb_B_Low->setValue(0);
    m_ui->Slider_rgb_B_High->setRange(0, 255);
    m_ui->Slider_rgb_B_High->setValue(120);

    // HSV
    m_ui->Slider_hsv_H_Low->setRange(0, 180);
    m_ui->Slider_hsv_H_Low->setValue(0);
    m_ui->Slider_hsv_H_High->setRange(0, 180);
    m_ui->Slider_hsv_H_High->setValue(120);
    m_ui->Slider_hsv_S_Low->setRange(0, 255);
    m_ui->Slider_hsv_S_Low->setValue(0);
    m_ui->Slider_hsv_S_High->setRange(0, 255);
    m_ui->Slider_hsv_S_High->setValue(120);
    m_ui->Slider_hsv_V_Low->setRange(0, 255);
    m_ui->Slider_hsv_V_Low->setValue(0);
    m_ui->Slider_hsv_V_High->setRange(0, 255);
    m_ui->Slider_hsv_V_High->setValue(120);

    // 初始化 spinbox 范围
    m_ui->spinBox_grayLow->setRange(0, 255);
    m_ui->spinBox_grayLow->setValue(0);
    m_ui->spinBox_grayHigh->setRange(0, 255);
    m_ui->spinBox_grayHigh->setValue(120);

    m_ui->spinBox_rgb_R_Low->setRange(0, 255);
    m_ui->spinBox_rgb_R_Low->setValue(0);
    m_ui->spinBox_rgb_R_High->setRange(0, 255);
    m_ui->spinBox_rgb_R_High->setValue(120);
    m_ui->spinBox_rgb_G_Low->setRange(0, 255);
    m_ui->spinBox_rgb_G_Low->setValue(0);
    m_ui->spinBox_rgb_G_High->setRange(0, 255);
    m_ui->spinBox_rgb_G_High->setValue(120);
    m_ui->spinBox_rgb_B_Low->setRange(0, 255);
    m_ui->spinBox_rgb_B_Low->setValue(0);
    m_ui->spinBox_rgb_B_High->setRange(0, 255);
    m_ui->spinBox_rgb_B_High->setValue(120);

    m_ui->spinBox_hsv_H_Low->setRange(0, 180);
    m_ui->spinBox_hsv_H_Low->setValue(0);
    m_ui->spinBox_hsv_H_High->setRange(0, 180);
    m_ui->spinBox_hsv_H_High->setValue(120);
    m_ui->spinBox_hsv_S_Low->setRange(0, 255);
    m_ui->spinBox_hsv_S_Low->setValue(0);
    m_ui->spinBox_hsv_S_High->setRange(0, 255);
    m_ui->spinBox_hsv_S_High->setValue(120);
    m_ui->spinBox_hsv_V_Low->setRange(0, 255);
    m_ui->spinBox_hsv_V_Low->setValue(0);
    m_ui->spinBox_hsv_V_High->setRange(0, 255);
    m_ui->spinBox_hsv_V_High->setValue(120);

    setupConnections();
}

FilterTabWidget::~FilterTabWidget()
{
    delete m_ui;
}

void FilterTabWidget::setupConnections()
{
    // ========== Slider ↔ SpinBox 双向同步 ==========
    bindSliderAndSpinBox(m_ui->Slider_grayLow, m_ui->spinBox_grayLow);
    bindSliderAndSpinBox(m_ui->Slider_grayHigh, m_ui->spinBox_grayHigh);
    bindSliderAndSpinBox(m_ui->Slider_rgb_R_Low, m_ui->spinBox_rgb_R_Low);
    bindSliderAndSpinBox(m_ui->Slider_rgb_R_High, m_ui->spinBox_rgb_R_High);
    bindSliderAndSpinBox(m_ui->Slider_rgb_G_Low, m_ui->spinBox_rgb_G_Low);
    bindSliderAndSpinBox(m_ui->Slider_rgb_G_High, m_ui->spinBox_rgb_G_High);
    bindSliderAndSpinBox(m_ui->Slider_rgb_B_Low, m_ui->spinBox_rgb_B_Low);
    bindSliderAndSpinBox(m_ui->Slider_rgb_B_High, m_ui->spinBox_rgb_B_High);
    bindSliderAndSpinBox(m_ui->Slider_hsv_H_Low, m_ui->spinBox_hsv_H_Low);
    bindSliderAndSpinBox(m_ui->Slider_hsv_H_High, m_ui->spinBox_hsv_H_High);
    bindSliderAndSpinBox(m_ui->Slider_hsv_S_Low, m_ui->spinBox_hsv_S_Low);
    bindSliderAndSpinBox(m_ui->Slider_hsv_S_High, m_ui->spinBox_hsv_S_High);
    bindSliderAndSpinBox(m_ui->Slider_hsv_V_Low, m_ui->spinBox_hsv_V_Low);
    bindSliderAndSpinBox(m_ui->Slider_hsv_V_High, m_ui->spinBox_hsv_V_High);

    // ========== 只有 Slider 变化才触发防抖和参数同步 ==========
    // 灰度滑块
    connect(m_ui->Slider_grayLow, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_grayHigh, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);

    // RGB 滑块
    connect(m_ui->Slider_rgb_R_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_rgb_R_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_rgb_G_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_rgb_G_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_rgb_B_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_rgb_B_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);

    // HSV 滑块
    connect(m_ui->Slider_hsv_H_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_hsv_H_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_hsv_S_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_hsv_S_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_hsv_V_Low, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);
    connect(m_ui->Slider_hsv_V_High, &QSlider::valueChanged,
            this, &FilterTabWidget::filterColorChannelsChanged);

    // 过滤模式变化 - 直接连接到槽函数
    connect(m_ui->comboBox_filterMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FilterTabWidget::onFilterModeChanged);
}



void FilterTabWidget::onFilterModeChanged(int index)
{
    m_ui->stackedWidget_filter->setCurrentIndex(index);
    m_pipeline->updateConfig([this](PipelineConfig& cfg) { saveToConfig(cfg); });
    emit filterConfigChanged();
}

void FilterTabWidget::filterColorChannelsChanged()
{
    m_pipeline->updateConfig([this](PipelineConfig& cfg) { saveToConfig(cfg); });
    emit filterConfigChanged();
}




void FilterTabWidget::getFilterConfig(int& mode, int& grayLow, int& grayHigh) const
{
    mode = m_ui->comboBox_filterMode->currentIndex();
    grayLow = m_ui->Slider_grayLow->value();
    grayHigh = m_ui->Slider_grayHigh->value();
}

void FilterTabWidget::setFilterConfig(int mode, int grayLow, int grayHigh)
{
    const QSignalBlocker modeBlocker(m_ui->comboBox_filterMode);
    const QSignalBlocker b1(m_ui->Slider_grayLow);
    const QSignalBlocker b2(m_ui->Slider_grayHigh);
    const QSignalBlocker sb1(m_ui->spinBox_grayLow);
    const QSignalBlocker sb2(m_ui->spinBox_grayHigh);

    m_ui->comboBox_filterMode->setCurrentIndex(mode);
    m_ui->stackedWidget_filter->setCurrentIndex(mode);
    m_ui->Slider_grayLow->setValue(grayLow);
    m_ui->Slider_grayHigh->setValue(grayHigh);
    m_ui->spinBox_grayLow->setValue(grayLow);
    m_ui->spinBox_grayHigh->setValue(grayHigh);
}

void FilterTabWidget::connectSignals(const SignalContext& ctx,
                                     std::function<void()> onExecutePipeline,
                                     std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    Q_UNUSED(ctx); Q_UNUSED(onConfigSaved);
    connect(this, &FilterTabWidget::filterConfigChanged,
            this, [onExecutePipeline]() { onExecutePipeline(); });
}

// ========== IConfigurableTab 接口实现 ==========

void FilterTabWidget::saveToConfig(PipelineConfig& config) const
{
    auto& cf = config.colorFilter;
    cf.mode = static_cast<ImageFilterMode>(m_ui->comboBox_filterMode->currentIndex());
    cf.grayLow = m_ui->Slider_grayLow->value();
    cf.grayHigh = m_ui->Slider_grayHigh->value();
    cf.rLow = m_ui->Slider_rgb_R_Low->value();
    cf.rHigh = m_ui->Slider_rgb_R_High->value();
    cf.gLow = m_ui->Slider_rgb_G_Low->value();
    cf.gHigh = m_ui->Slider_rgb_G_High->value();
    cf.bLow = m_ui->Slider_rgb_B_Low->value();
    cf.bHigh = m_ui->Slider_rgb_B_High->value();
    cf.hLow = m_ui->Slider_hsv_H_Low->value();
    cf.hHigh = m_ui->Slider_hsv_H_High->value();
    cf.sLow = m_ui->Slider_hsv_S_Low->value();
    cf.sHigh = m_ui->Slider_hsv_S_High->value();
    cf.vLow = m_ui->Slider_hsv_V_Low->value();
    cf.vHigh = m_ui->Slider_hsv_V_High->value();
}

void FilterTabWidget::loadFromConfig(const PipelineConfig& config)
{
    const ScopedWidgetSignals rendering(this);
    const auto& cf = config.colorFilter;
    m_ui->comboBox_filterMode->setCurrentIndex(static_cast<int>(cf.mode));
    m_ui->stackedWidget_filter->setCurrentIndex(static_cast<int>(cf.mode));
    m_ui->Slider_grayLow->setValue(cf.grayLow);
    m_ui->spinBox_grayLow->setValue(cf.grayLow);
    m_ui->Slider_grayHigh->setValue(cf.grayHigh);
    m_ui->spinBox_grayHigh->setValue(cf.grayHigh);
    m_ui->Slider_rgb_R_Low->setValue(cf.rLow);
    m_ui->spinBox_rgb_R_Low->setValue(cf.rLow);
    m_ui->Slider_rgb_R_High->setValue(cf.rHigh);
    m_ui->spinBox_rgb_R_High->setValue(cf.rHigh);
    m_ui->Slider_rgb_G_Low->setValue(cf.gLow);
    m_ui->spinBox_rgb_G_Low->setValue(cf.gLow);
    m_ui->Slider_rgb_G_High->setValue(cf.gHigh);
    m_ui->spinBox_rgb_G_High->setValue(cf.gHigh);
    m_ui->Slider_rgb_B_Low->setValue(cf.bLow);
    m_ui->spinBox_rgb_B_Low->setValue(cf.bLow);
    m_ui->Slider_rgb_B_High->setValue(cf.bHigh);
    m_ui->spinBox_rgb_B_High->setValue(cf.bHigh);
    m_ui->Slider_hsv_H_Low->setValue(cf.hLow);
    m_ui->spinBox_hsv_H_Low->setValue(cf.hLow);
    m_ui->Slider_hsv_H_High->setValue(cf.hHigh);
    m_ui->spinBox_hsv_H_High->setValue(cf.hHigh);
    m_ui->Slider_hsv_S_Low->setValue(cf.sLow);
    m_ui->spinBox_hsv_S_Low->setValue(cf.sLow);
    m_ui->Slider_hsv_S_High->setValue(cf.sHigh);
    m_ui->spinBox_hsv_S_High->setValue(cf.sHigh);
    m_ui->Slider_hsv_V_Low->setValue(cf.vLow);
    m_ui->spinBox_hsv_V_Low->setValue(cf.vLow);
    m_ui->Slider_hsv_V_High->setValue(cf.vHigh);
    m_ui->spinBox_hsv_V_High->setValue(cf.vHigh);
}
