#include "image_tab_widget.h"
#include "ui_image_tab.h"
#include "roi_manager.h"
#include "pipeline_manager.h"

ImageTabWidget::ImageTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::ImageTabWidget)
    , m_pipeline(pipelineAccess)
{
    m_ui->setupUi(this);
    connect(m_ui->btn_applyChannel, &QPushButton::clicked, this, &ImageTabWidget::onApplyChannelClicked);
}

ImageTabWidget::~ImageTabWidget()
{
    delete m_ui;
}

void ImageTabWidget::saveToConfig(PipelineConfig& config) const
{
    config.colorFilter.channel = channelFromIndex(m_ui->comboBox_channels->currentIndex());
}

void ImageTabWidget::loadFromConfig(const PipelineConfig& config)
{
    const QSignalBlocker blocker(m_ui->comboBox_channels);
    m_ui->comboBox_channels->setCurrentIndex(static_cast<int>(config.colorFilter.channel));
}

PipelineConfig::Channel ImageTabWidget::channelFromIndex(int index) const
{
    switch (index)
    {
    case 0: return PipelineConfig::Channel::Gray;
    case 1: return PipelineConfig::Channel::RGB;
    case 2: return PipelineConfig::Channel::HSV;
    case 3: return PipelineConfig::Channel::B;
    case 4: return PipelineConfig::Channel::G;
    case 5: return PipelineConfig::Channel::R;
    default: return PipelineConfig::Channel::RGB;
    }
}

void ImageTabWidget::onApplyChannelClicked()
{
    if (!m_ui || !m_pipeline) return;

    int comboIndex = m_ui->comboBox_channels->currentIndex();
    PipelineConfig::Channel channel = channelFromIndex(comboIndex);

    // 设置通道模式到Pipeline
    m_pipeline->updateConfig([&](PipelineConfig& cfg) {
        cfg.colorFilter.channel = channel;
    });

    emit channelChanged(comboIndex);
}


void ImageTabWidget::connectSignals(const SignalContext& ctx,
                                    std::function<void()> onExecutePipeline,
                                    std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    connect(this, &ImageTabWidget::channelChanged,
            this, [onExecutePipeline](int) {
                if (onExecutePipeline) onExecutePipeline();
            });
}

