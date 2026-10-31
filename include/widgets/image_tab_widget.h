#ifndef IMAGE_TAB_WIDGET_H
#define IMAGE_TAB_WIDGET_H

#include <QWidget>
#include "core/i_pipeline_access.h"
#include "widgets/i_tab_interfaces.h"

namespace Ui {
class ImageTabWidget;
}

class ImageTabWidget : public QWidget, public ISignalConnectable, public IConfigurableTab {
    Q_OBJECT

public:
    explicit ImageTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent = nullptr);
    ~ImageTabWidget();
    void saveToConfig(PipelineConfig& config) const override;
    void loadFromConfig(const PipelineConfig& config) override;

    void connectSignals(const SignalContext& ctx,
                        std::function<void()> onExecutePipeline,
                        std::function<void()> onConfigSaved = nullptr) override;

signals:
    void channelChanged(int channel);

private slots:
    void onApplyChannelClicked();

private:
    PipelineConfig::Channel channelFromIndex(int index) const;

    Ui::ImageTabWidget* m_ui;
    IPipelineAccess* m_pipeline;
};

#endif // IMAGE_TAB_WIDGET_H
