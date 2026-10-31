#include "application/remote_inspection_controller.h"
#include "core/mqtt_manager.h"
#include "core/pipeline_manager.h"
#include "core/roi_manager.h"
#include "controllers/auto_detection_controller.h"
#include "widgets/tab_manager.h"
#include "widgets/video_tab_widget.h"
#include <QtConcurrent/QtConcurrent>
#include <QThread>

namespace {
cv::Mat captureCamera()
{
    cv::VideoCapture camera;
    // Default device must work on machines with a single camera, too.
    if (!camera.open(0, cv::CAP_DSHOW) && !camera.open(0)) return {};
    QThread::msleep(300);
    cv::Mat frame;
    if (!camera.read(frame)) return {};
    cv::flip(frame, frame, 1);
    return frame;
}
}

RemoteInspectionController::RemoteInspectionController(MqttManager* mqtt,
    AutoDetectionController* detection, RoiManager* rois, PipelineManager* pipeline,
    TabManager* tabs, QObject* parent, Capture capture)
    : QObject(parent), m_mqtt(mqtt), m_rois(rois), m_pipeline(pipeline),
      m_tabs(tabs), m_capture(capture ? std::move(capture) : captureCamera)
{
    connect(detection, &AutoDetectionController::imageProcessed, this,
        [mqtt, detection](int index, const QString&, bool) {
            const auto& results = detection->results();
            if (index >= 0 && index < results.size()) mqtt->publishResult(results[index]);
        });
    connect(mqtt, &MqttManager::captureRequested, this, &RemoteInspectionController::capture);
    connect(mqtt, &MqttManager::startDetectionRequested, this, [this] {
        if (!m_stopped) emit startDetectionRequested();
    });
    connect(mqtt, &MqttManager::stopDetectionRequested, detection, &AutoDetectionController::stopDetection);
    connect(mqtt, &MqttManager::pingRequested, this, [this] {
        if (!m_stopped) emit statusMessage(QStringLiteral("收到云端指令: Ping - 已回复心跳"));
    });
    connect(&m_watcher, &QFutureWatcher<cv::Mat>::finished, this, [this] {
        m_busy = false;
        if (m_stopped) return;
        if (m_imageId != m_rois->getCurrentImageId() || m_generation != m_pipeline->displayGeneration()
            || m_config != m_pipeline->getConfigSnapshot().toJson()) {
            emit statusMessage(QStringLiteral("采集期间图片或配置已切换，已丢弃过期帧"));
            return;
        }
        acceptFrame(m_watcher.result());
    });
}

RemoteInspectionController::~RemoteInspectionController() { shutdown(); }

void RemoteInspectionController::capture()
{
    if (m_stopped || m_busy) return;
    // The video device is owned by the UI thread. Read its cached frame rather
    // than advancing the stream or sharing the device with a second thread.
    if (auto* tab = m_tabs->getTabAs<VideoTabWidget>("视频")) {
        if (auto* video = tab->getVideoManager(); video->isOpened()) {
            acceptFrame(video->getCurrentFrame());
            return;
        }
    }
    m_busy = true;
    m_imageId = m_rois->getCurrentImageId();
    m_generation = m_pipeline->displayGeneration();
    m_config = m_pipeline->getConfigSnapshot().toJson();
    emit statusMessage(QStringLiteral("正在打开相机采集..."));
    m_watcher.setFuture(QtConcurrent::run([capture = m_capture] {
        try { return capture(); }
        catch (...) { return cv::Mat{}; }
    }));
}

void RemoteInspectionController::acceptFrame(const cv::Mat& frame)
{
    if (frame.empty()) {
        emit statusMessage(QStringLiteral("无法获取图像，请手动打开相机"));
        return;
    }
    m_rois->setFullImage(frame);
    emit statusMessage(QStringLiteral("已采集新帧，正在处理..."));
    emit refreshRequested();
}

void RemoteInspectionController::shutdown()
{
    if (m_stopped) return;
    m_stopped = true;
    m_mqtt->stopHeartbeat();
    disconnect(m_mqtt, nullptr, this, nullptr);
    m_watcher.disconnect(this);
    m_watcher.waitForFinished();
}
