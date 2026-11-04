#include "line_tab_widget.h"
#include "ui_line_tab.h"
#include "logger.h"
#include "ui/slider_spinbox_binder.h"
#include "ui/scoped_widget_signals.h"
#include "image_view.h"
#include "controllers/roi_ui_controller.h"
#include <QMessageBox>

LineDetectTabWidget::LineDetectTabWidget(IPipelineAccess* pipelineAccess,
                                         QWidget* parent)
    : QWidget(parent)
    , m_pipeline(pipelineAccess)
{
    m_ui = new Ui::LineTabForm();
    m_ui->setupUi(this);
}

LineDetectTabWidget::~LineDetectTabWidget()
{
    delete m_ui;
}

void LineDetectTabWidget::updateFromPipeline(const PipelineContext& ctx)
{
    PipelineConfig cfg = m_pipeline->getConfigSnapshot();
    updateMatchResultStatus(
        ctx.matchedLineCount,
        ctx.totalLineCount,
        cfg.lineDetect.angleThreshold,
        cfg.lineDetect.distanceThreshold,
        cfg.lineDetect.enableReferenceLineMatch
    );
}

void LineDetectTabWidget::initialize()
{
    if (m_initialized) return;
    m_initialized = true;
    const ScopedWidgetSignals rendering(this);
    // 设置Slider范围和步长
    m_ui->Slider_HoughPThreshold->setRange(1, 200);
    m_ui->Slider_HoughPThreshold->setSingleStep(1);
    m_ui->Slider_HoughPThreshold->setValue(50);

    m_ui->Slider_HoughPMinLineLength->setRange(1, 500);
    m_ui->Slider_HoughPMinLineLength->setSingleStep(1);
    m_ui->Slider_HoughPMinLineLength->setValue(30);

    m_ui->Slider_HoughPMaxLineGap->setRange(0, 100);
    m_ui->Slider_HoughPMaxLineGap->setSingleStep(1);
    m_ui->Slider_HoughPMaxLineGap->setValue(10);

    // 设置SpinBox范围和步长
    m_ui->SpinBox_HoughPThreshold->setRange(1, 200);
    m_ui->SpinBox_HoughPThreshold->setSingleStep(1);
    m_ui->SpinBox_HoughPThreshold->setValue(50);
    
    m_ui->SpinBox_HoughPMinLineLength->setRange(1, 500);
    m_ui->SpinBox_HoughPMinLineLength->setSingleStep(1);
    m_ui->SpinBox_HoughPMinLineLength->setValue(30);

    m_ui->SpinBox_HoughPMaxLineGap->setRange(0, 100);
    m_ui->SpinBox_HoughPMaxLineGap->setSingleStep(1);
    m_ui->SpinBox_HoughPMaxLineGap->setValue(10);

    connect(m_ui->comboBox_lineAlgorithm,&QComboBox::currentIndexChanged,this,&LineDetectTabWidget::onLineAlgorithmChanged);
    connect(m_ui->btn_applyLineDetecter,&QPushButton::clicked,this,&LineDetectTabWidget::handleApply);

    // Slider变化时触发Pipeline（SpinBox通过bindSliderAndSpinBox同步值，不重复连接）
    connect(m_ui->Slider_HoughPThreshold,&QSlider::valueChanged,this,&LineDetectTabWidget::onLineThresholdChanged);
    connect(m_ui->Slider_HoughPMinLineLength,&QSlider::valueChanged,this,&LineDetectTabWidget::onLineMinLengthChanged);
    connect(m_ui->Slider_HoughPMaxLineGap,&QSlider::valueChanged,this,&LineDetectTabWidget::onLineMaxGapChanged);

    bindSliderAndSpinBox(m_ui->Slider_HoughPThreshold, m_ui->SpinBox_HoughPThreshold);
    bindSliderAndSpinBox(m_ui->Slider_HoughPMinLineLength, m_ui->SpinBox_HoughPMinLineLength);
    bindSliderAndSpinBox(m_ui->Slider_HoughPMaxLineGap, m_ui->SpinBox_HoughPMaxLineGap);

    // ========== 参考线匹配信号连接 ==========
    connect(m_ui->chk_enableReferenceLine, &QCheckBox::toggled, this, &LineDetectTabWidget::onReferenceLineEnabledChanged);
    connect(m_ui->btn_drawReferenceLine, &QPushButton::clicked, this, &LineDetectTabWidget::onDrawReferenceLineClicked);
    connect(m_ui->btn_clearReferenceLine, &QPushButton::clicked, this, &LineDetectTabWidget::onClearReferenceLineClicked);
    connect(m_ui->spin_angleThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LineDetectTabWidget::onAngleThresholdChanged);
    connect(m_ui->spin_distanceThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LineDetectTabWidget::onDistanceThresholdChanged);
    connect(m_ui->spin_searchRegionWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, &LineDetectTabWidget::onSearchRegionWidthChanged);

    // 初始化参考线匹配控件状态
    m_ui->groupBox_referenceLine->setEnabled(true);
    setLineState(m_pipeline ? m_pipeline->getConfigSnapshot().lineDetect : LineDetectConfig{});

}

void LineDetectTabWidget::onLineAlgorithmChanged(int value)
{
    m_ui->stackedWidget_LineDetect->setCurrentIndex(value == 0 ? 1 : 0);
    commitLineConfig(getLineState(), false);
}

void LineDetectTabWidget::onLineThresholdChanged(int value)
{
    auto state = getLineState();
    state.threshold = value;
    commitLineConfig(state, state.enabled);
}

void LineDetectTabWidget::onLineMinLengthChanged(int value)
{
    auto state = getLineState();
    state.minLength = value;
    commitLineConfig(state, state.enabled);
}

void LineDetectTabWidget::onLineMaxGapChanged(int value)
{
    auto state = getLineState();
    state.maxGap = value;
    commitLineConfig(state, state.enabled);
}

void LineDetectTabWidget::handleApply()
{
    if (!m_pipeline) return;
    auto state = getLineState();
    state.enabled = !(state.enableReferenceLineMatch && state.referenceLineValid);
    m_pipeline->setDisplayMode(DisplayConfig::Mode::LineDetect);
    commitLineConfig(state, true);
}

LineDetectTabWidget::LineDetectState LineDetectTabWidget::getLineState() const
{
    LineDetectState state = m_pipeline ? m_pipeline->getConfigSnapshot().lineDetect : LineDetectState{};
    state.algorithm = m_ui->comboBox_lineAlgorithm->currentIndex();
    state.threshold = m_ui->Slider_HoughPThreshold->value();
    state.minLength = m_ui->Slider_HoughPMinLineLength->value();
    state.maxGap = m_ui->Slider_HoughPMaxLineGap->value();
    return state;
}

void LineDetectTabWidget::setLineState(const LineDetectState &state)
{
    const ScopedWidgetSignals rendering(this);
    const QSignalBlocker b1(m_ui->Slider_HoughPThreshold);
    const QSignalBlocker b2(m_ui->Slider_HoughPMinLineLength);
    const QSignalBlocker b3(m_ui->Slider_HoughPMaxLineGap);
    const QSignalBlocker sb1(m_ui->SpinBox_HoughPThreshold);
    const QSignalBlocker sb2(m_ui->SpinBox_HoughPMinLineLength);
    const QSignalBlocker sb3(m_ui->SpinBox_HoughPMaxLineGap);

    m_ui->comboBox_lineAlgorithm->setCurrentIndex(state.algorithm);
    m_ui->stackedWidget_LineDetect->setCurrentIndex(state.algorithm == 0 ? 1 : 0);
    m_ui->Slider_HoughPThreshold->setValue(state.threshold);
    m_ui->Slider_HoughPMinLineLength->setValue(state.minLength);
    m_ui->Slider_HoughPMaxLineGap->setValue(state.maxGap);

    m_ui->SpinBox_HoughPThreshold->setValue(state.threshold);
    m_ui->SpinBox_HoughPMinLineLength->setValue(state.minLength);
    m_ui->SpinBox_HoughPMaxLineGap->setValue(state.maxGap);
    m_ui->chk_enableReferenceLine->setChecked(state.enableReferenceLineMatch);
    m_ui->spin_angleThreshold->setValue(state.angleThreshold);
    m_ui->spin_distanceThreshold->setValue(state.distanceThreshold);
    m_ui->spin_searchRegionWidth->setValue(state.searchRegionWidth);
    renderReferenceLineStatus(state);
}

void LineDetectTabWidget::setLineConfig(const LineDetectConfig& config)
{
    setLineState(config);
}

// ========== 参考线匹配槽函数 ==========

void LineDetectTabWidget::onReferenceLineEnabledChanged(bool enabled)
{
    auto state = getLineState();
    state.enableReferenceLineMatch = enabled;
    if (enabled) state.enabled = false;
    commitLineConfig(state, true);
}

void LineDetectTabWidget::onDrawReferenceLineClicked()
{
    // 发射信号请求绘制参考线
    emit requestDrawReferenceLine();
    
    // 提示用户（不弹窗，只在日志显示）
    spdlog::info("请在图像上点击绘制参考线的起点和终点");
}

void LineDetectTabWidget::onClearReferenceLineClicked()
{
    auto state = getLineState();
    state.referenceLineValid = false;
    state.referenceLineStart = {};
    state.referenceLineEnd = {};
    commitLineConfig(state, false);
    emit requestClearReferenceLine();
    if (m_onExecutePipeline) m_onExecutePipeline();
}

void LineDetectTabWidget::onAngleThresholdChanged(double value)
{
    auto state = getLineState();
    state.angleThreshold = value;
    commitLineConfig(state, state.enableReferenceLineMatch && state.referenceLineValid);
}

void LineDetectTabWidget::onDistanceThresholdChanged(double value)
{
    auto state = getLineState();
    state.distanceThreshold = value;
    commitLineConfig(state, state.enableReferenceLineMatch && state.referenceLineValid);
}

void LineDetectTabWidget::onSearchRegionWidthChanged(int value)
{
    auto state = getLineState();
    state.searchRegionWidth = value;
    commitLineConfig(state, state.enableReferenceLineMatch && state.referenceLineValid);
}

void LineDetectTabWidget::updateReferenceLineStatus()
{
    if (m_pipeline) renderReferenceLineStatus(m_pipeline->getConfigSnapshot().lineDetect);
}

void LineDetectTabWidget::renderReferenceLineStatus(const LineDetectConfig& state)
{
    QString status;

    if (!state.enableReferenceLineMatch) {
        status = "状态：参考线匹配未启用";
    } else if (!state.referenceLineValid) {
        status = "状态：参考线未绘制\n点击\"绘制参考线\"按钮开始";
    } else {
        status = QString("状态：参考线已绘制\n起点:(%1,%2)\n终点:(%3,%4)")
                     .arg(state.referenceLineStart.x, 0, 'f', 1)
                     .arg(state.referenceLineStart.y, 0, 'f', 1)
                     .arg(state.referenceLineEnd.x, 0, 'f', 1)
                     .arg(state.referenceLineEnd.y, 0, 'f', 1);
    }

    m_ui->label_referenceLineStatus->setText(status);
}

void LineDetectTabWidget::connectSignals(const SignalContext& ctx,
                                         std::function<void()> onExecutePipeline,
                                         std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    Q_UNUSED(onConfigSaved);
    m_onExecutePipeline = onExecutePipeline;

    if (ctx.roiCtrl)
        connect(this, &LineDetectTabWidget::lineConfigEdited,
                ctx.roiCtrl, &RoiUiController::updateLineDetectionConfig);

    connect(this, &LineDetectTabWidget::requestDrawReferenceLine,
            ctx.view, &ImageView::startReferenceLineDrawing);
    connect(this, &LineDetectTabWidget::requestClearReferenceLine,
            ctx.view, &ImageView::clearReferenceLine);
    connect(ctx.view, &ImageView::referenceLineDrawn,
            this, &LineDetectTabWidget::setReferenceLine);
}

void LineDetectTabWidget::setReferenceLine(const cv::Point2f& start, const cv::Point2f& end)
{
    auto state = getLineState();
    state.referenceLineStart = start;
    state.referenceLineEnd = end;
    state.referenceLineValid = true;
    state.enableReferenceLineMatch = true;
    state.enabled = false;
    {
        const QSignalBlocker blocker(m_ui->chk_enableReferenceLine);
        m_ui->chk_enableReferenceLine->setChecked(true);
    }
    commitLineConfig(state, true);
}

void LineDetectTabWidget::updateMatchResultStatus(int matchedCount, int totalCount, double angleThreshold, double distanceThreshold, bool isReferenceMode)
{
    if (!m_ui) return;

    QString status;

    if (isReferenceMode) {
        if (matchedCount > 0) {
            status = QString("匹配结果: %1/%2 条直线匹配成功\n角度容差: %3°, 距离容差: %4px")
                         .arg(matchedCount)
                         .arg(totalCount)
                         .arg(angleThreshold)
                         .arg(distanceThreshold);
        } else if (totalCount > 0) {
            status = QString("匹配结果: 未找到匹配直线 (检测到 %1 条)\n角度容差: %2°, 距离容差: %3px")
                         .arg(totalCount)
                         .arg(angleThreshold)
                         .arg(distanceThreshold);
        } else {
            status = QString("匹配结果: 未找到匹配直线\n角度容差: %1°, 距离容差: %2px")
                         .arg(angleThreshold)
                         .arg(distanceThreshold);
        }
    } else {
        if (totalCount > 0) {
            status = QString("直线检测完成: 共检测到 %1 条直线").arg(totalCount);
        } else {
            status = QString("未检测到直线");
        }
    }

    m_ui->label_referenceLineStatus->setText(status);
}


void LineDetectTabWidget::commitLineConfig(LineDetectConfig state, bool execute)
{
    if (!m_pipeline) return;
    auto config = m_pipeline->getConfigSnapshot();
    state.enhance = config.enhance;
    config.lineDetect = state;
    m_pipeline->setConfig(config);
    emit lineConfigEdited(state);
    renderReferenceLineStatus(state);
    if (execute && m_onExecutePipeline) m_onExecutePipeline();
}
