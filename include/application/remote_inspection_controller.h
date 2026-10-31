#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QJsonObject>
#include <opencv2/core/mat.hpp>
#include <functional>

class MqttManager;
class AutoDetectionController;
class RoiManager;
class PipelineManager;
class TabManager;

class RemoteInspectionController : public QObject {
    Q_OBJECT
public:
    using Capture = std::function<cv::Mat()>;
    RemoteInspectionController(MqttManager* mqtt, AutoDetectionController* detection,
        RoiManager* rois, PipelineManager* pipeline, TabManager* tabs,
        QObject* parent = nullptr, Capture capture = {});
    ~RemoteInspectionController() override;
    void capture();
    void shutdown();
signals:
    void statusMessage(const QString& message);
    void refreshRequested();
    void startDetectionRequested();
private:
    void acceptFrame(const cv::Mat& frame);
    MqttManager* m_mqtt;
    RoiManager* m_rois;
    PipelineManager* m_pipeline;
    TabManager* m_tabs;
    Capture m_capture;
    QFutureWatcher<cv::Mat> m_watcher;
    QString m_imageId;
    quint64 m_generation = 0;
    QJsonObject m_config;
    bool m_busy = false;
    bool m_stopped = false;
};
