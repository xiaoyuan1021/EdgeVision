#pragma once

#include <QWidget>
#include <QTimer>
#include <QThread>
#include "core/i_pipeline_access.h"
#include "data/object_detection_result.h"
#include <vector>
#include "widgets/i_tab_interfaces.h"

namespace Ui {
class ObjectDetectionTabForm;
}
class ObjectDetectionTabWidget : public QWidget, public ISignalConnectable, public IResultUpdatable {
    Q_OBJECT

public:
    explicit ObjectDetectionTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent = nullptr);
    ~ObjectDetectionTabWidget();

    void updateFromPipeline(const PipelineContext& context) override;
    void clearResults() override { updateDetectionResults({}); }

    float getConfidenceThreshold() const;
    float getNmsThreshold() const;

    /**
     * [NOTE] 获取当前配置（用于同步到DetectionItem.config）
     */
    ObjectDetectionConfig getDetectionConfig() const;

    /**
     * [NOTE] 设置检测配置（从DetectionItem.config恢复到UI）
     */
    void setDetectionConfig(const ObjectDetectionConfig& config);

    /**
     * 更新检测结果显示
     */
    void updateDetectionResults(const std::vector<DetectionResult>& results);

    void connectSignals(const SignalContext& ctx,
                        std::function<void()> onExecutePipeline,
                        std::function<void()> onConfigSaved = nullptr) override;

signals:
    void detectionConfigChanged();
    void modelLoadFinished(bool success, const QString& message);

private slots:
    void onApplyClicked();
    void onBrowseModel();
    void onBrowseConfig();
    void autoLoadDefaultModel();
    void loadModelAsync();

private:
    void setupConnections();

    Ui::ObjectDetectionTabForm* m_ui;
    IPipelineAccess* m_pipeline;
    ObjectDetectionConfig m_renderedConfig;
    quint64 m_loadGeneration = 0;
};
