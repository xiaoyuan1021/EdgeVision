#include "core/pipeline_executor.h"
#include "algorithm/object_detection_service.h"
#include "controllers/auto_detection_controller.h"
#include "logger.h"
#include "config/detection_config_types.h"
#include "algorithm/image_utils.h"
#include "core/roi_inspection_runner.h"
#include "data/roi_detection_result.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

AutoDetectionController::AutoDetectionController(
    RoiManager* roiManager,
    QObject* parent)
    : QObject(parent)
    , m_roiManager(roiManager)
    , m_watcher(new QFutureWatcher<ImageDetectionResult>(this))
{
    connect(m_watcher, &QFutureWatcher<ImageDetectionResult>::finished,
            this, &AutoDetectionController::onTaskFinished);
}

// ==================== 控制接口 ====================

AutoDetectionController::~AutoDetectionController() { shutdown(); }

void AutoDetectionController::shutdown()
{
    if (m_shutdown) return;
    m_shutdown = true;
    m_running = false;
    m_watcher->disconnect(this);
    m_watcher->waitForFinished();
    m_taskInFlight = false;
}

void AutoDetectionController::startDetection()
{
    if (m_shutdown) return;
    if (m_running) {
        emit logMessage("检测已在运行中，请先停止当前检测");
        return;
    }

    // 从 RoiManager 构建任务列表
    m_tasks = buildTaskList();

    if (m_tasks.isEmpty()) {
        emit logMessage("没有要检测的图片，请先添加图片并配置ROI和检测项");
        return;
    }

    // 重置状态
    m_currentIndex = 0;
    m_stopRequested = false;
    m_paused = false;
    m_running = true;

    m_stats.reset();
    m_stats.totalCount = m_tasks.size();
    m_reports.clear();
    m_records.clear();
    for (const auto& task : m_tasks) {
        InspectionRecord record;
        record.result.imageId = task.imageId;
        record.result.imageName = task.imageName;
        record.result.imagePath = task.imagePath;
        m_records.append(std::move(record));
    }
    m_failedImages.clear();

    m_elapsedTimer.start();

    emit detectionStarted(m_stats.totalCount);
    emit recordsChanged();
    emit logMessage(QString("开始批量检测: 共 %1 张图片").arg(m_stats.totalCount));

    // 开始处理第一个任务
    processNextTask();
}

void AutoDetectionController::stopDetection()
{
    if (!m_running) {
        return;
    }

    m_stopRequested = true;
    m_paused = false;
    emit logMessage("正在停止检测...");
    processNextTask();
}

void AutoDetectionController::pauseDetection()
{
    if (!m_running || m_paused) {
        return;
    }

    m_paused = true;
    emit logMessage("检测已暂停");
}

void AutoDetectionController::resumeDetection()
{
    if (!m_running || !m_paused) {
        return;
    }

    m_paused = false;
    emit logMessage("检测已恢复");

    // 继续处理下一个任务
    processNextTask();
}

// ==================== 状态查询 ====================

QString AutoDetectionController::summaryString() const
{
    return QString("总计: %1 | 已处理: %2 | 合格: %3 | 不合格: %4 | 合格率: %5% | 耗时: %6s | 平均: %7ms/张")
        .arg(m_stats.totalCount)
        .arg(m_stats.processedCount)
        .arg(m_stats.passCount)
        .arg(m_stats.failCount)
        .arg(m_stats.passRate(), 0, 'f', 1)
        .arg(m_stats.elapsedMs / 1000.0, 0, 'f', 1)
        .arg(m_stats.avgTimePerImage(), 0, 'f', 0);
}

// ==================== 内部实现 ====================

QList<ImageDetectionTask> AutoDetectionController::buildTaskList()
{
    QList<ImageDetectionTask> tasks;

    QStringList imageIds = m_roiManager->getImageIds();

    for (const QString& imageId : imageIds) {
        ImageDetectionTask task;
        task.imageId = imageId;
        task.imagePath = m_roiManager->getImageFilePath(imageId);
        task.imageName = m_roiManager->getImageName(imageId);
        task.roiConfigs = m_roiManager->getRoiConfigsForImage(imageId);

        // 无 ROI 的图片不参与批量检测，也不分配快照。
        if (task.roiConfigs.isEmpty()) {
            spdlog::debug(QString("[buildTaskList] 跳过图片 %1: 没有ROI配置").arg(imageId));
            continue;
        }

        task.image = m_roiManager->getImage(imageId).clone();
        tasks.append(std::move(task));
    }

    spdlog::info(QString("[buildTaskList] 构建了 %1 个检测任务（共 %2 张图片）")
        .arg(tasks.size()).arg(imageIds.size()));
    return tasks;
}

void AutoDetectionController::processNextTask()
{
    // Resume/stop may arrive while a watcher is still processing this image.
    if (!m_running || m_taskInFlight) return;
    // 检查是否需要停止
    if (m_stopRequested) {
        finishDetection();
        return;
    }

    // 检查是否暂停
    if (m_paused) {
        return;
    }

    // 检查是否处理完毕
    if (m_currentIndex >= m_tasks.size()) {
        finishDetection();
        return;
    }

    // 处理当前任务
    processImageTask(m_tasks[m_currentIndex]);
}

ImageDetectionResult AutoDetectionController::executeTask(const ImageDetectionTask& task)
{
    ImageDetectionResult result;
    result.imageId = task.imageId;
    result.imageName = task.imageName;
    result.imagePath = task.imagePath;

    // 任务只读取启动时的快照，不重新打开可能已被替换的磁盘文件。
    const cv::Mat& image = task.image;
    if (image.empty()) {
        result.passed = false;
        result.failReason = QStringLiteral("检测任务没有图像快照");
        spdlog::error("[检测] {}: {}", task.imageId.toStdString(), result.failReason.toStdString());
        return result;
    }

    try {
        for (const RoiConfig& roi : task.roiConfigs) {
            if (!roi.isActive) continue;
            const cv::Rect rect = ImageUtils::mapRoiToCvRect(roi.roiRect, image.cols, image.rows);
            if (rect.empty()) {
                RoiDetectionResult invalid(roi.roiId, roi.roiName, task.imageId);
                invalid.passed = false;
                invalid.failReason = QStringLiteral("ROI 不在图像有效范围内");
                result.addRoiResult(invalid);
                continue;
            }
            const cv::Mat roiImage = image(rect).clone();
            auto roiResult = RoiInspectionRunner::run(
                roi, roiImage, PipelineExecutor::preprocess, ObjectDetectionService::detect);
            roiResult.imageId = task.imageId;
            result.addRoiResult(roiResult);
        }
    } catch (const cv::Exception& ex) {
        result.passed = false;
        result.failReason = QString("OpenCV错误: %1").arg(ex.what());
        spdlog::error("[检测] {}", result.failReason.toStdString());
    } catch (const std::exception& ex) {
        result.passed = false;
        result.failReason = QString("异常: %1").arg(ex.what());
        spdlog::error("[检测] {}", result.failReason.toStdString());
    }
    return result;
}

void AutoDetectionController::processImageTask(const ImageDetectionTask& task)
{
    m_taskInFlight = true;
    m_taskTimer.start();
    // 不捕获 Controller；窗口关闭后等待的只有独立任务数据。
    m_watcher->setFuture(QtConcurrent::run([task] { return executeTask(task); }));
}

void AutoDetectionController::onTaskFinished()
{
    const auto imageResult = m_watcher->result();
    const auto& task = m_tasks[m_currentIndex];
    const int currentIndex = m_currentIndex;
    const int totalCount = m_stats.totalCount;
    auto& record = m_records[currentIndex];
    record.result = imageResult;
    record.status = imageResult.passed ? InspectionStatus::Passed : InspectionStatus::Failed;
    record.completedAtMs = QDateTime::currentMSecsSinceEpoch();
    record.elapsedMs = m_taskTimer.elapsed();
    // 更新统计
    m_stats.processedCount++;
    m_stats.elapsedMs = m_elapsedTimer.elapsed();

    bool passed = imageResult.passed;
    if (passed) {
        m_stats.passCount++;
    } else {
        m_stats.failCount++;
        m_failedImages.append(task.imagePath);
    }

    // 生成检测报告，包含每个ROI的检测项结果
    DetectionResultReport report;
    report.imageId = task.imageId;
    report.imageName = task.imageName;
    report.passed = imageResult.passed;
    report.failReason = imageResult.failReason;

    // 汇总所有ROI的检测项结果
    for (const auto& roiResult : imageResult.roiResults) {
        for (const auto& itemResult : roiResult.itemResults) {
            DetectionItemReport itemReport;
            itemReport.itemName = itemResult.itemName;
            itemReport.detectionType = itemResult.detectionType;
            itemReport.passed = itemResult.passed;
            itemReport.failReason = itemResult.failReason;
            report.itemResults.append(itemReport);
        }
    }

    m_reports.append(report);
    emit recordsChanged();

    // 发送信号
    emit imageProcessed(currentIndex, task.imagePath, passed);
    emit progressUpdated(currentIndex + 1, totalCount);
    emit statsUpdated(m_stats);

    // 记录日志，显示每个ROI的独立结果
    QString status = passed ? "PASS" : "FAIL";
    QString roiSummary;
    for (const auto& roiResult : imageResult.roiResults) {
        if (!roiResult.passed) {
            if (!roiSummary.isEmpty()) roiSummary += "; ";
            roiSummary += QString("%1:%2").arg(roiResult.roiName, roiResult.failReason);
        }
    }
    emit logMessage(QString("[%1/%2] %3 -> %4 (%5)")
        .arg(currentIndex + 1)
        .arg(totalCount)
        .arg(task.imageName)
        .arg(status)
        .arg(roiSummary.isEmpty() ? "OK" : roiSummary));

    // 处理下一个任务
    m_currentIndex++;
    m_taskInFlight = false;
    processNextTask();
}

void AutoDetectionController::finishDetection()
{
    m_running = false;
    m_paused = false;
    m_stopRequested = false;
    m_stats.elapsedMs = m_elapsedTimer.elapsed();

    if (m_currentIndex >= m_tasks.size()) {
        emit logMessage(QString("批量检测完成! %1").arg(summaryString()));
    } else {
        emit logMessage(QString("批量检测已停止! %1").arg(summaryString()));
        emit detectionStopped();
    }

    emit detectionFinished(m_stats);
}

void AutoDetectionController::setupUiConnections(
    QPushButton* startBtn, QPushButton* stopBtn,
    QStatusBar* statusBar, QLabel* statusLabel)
{
    // 日志消息
    connect(this, &AutoDetectionController::logMessage,
            this, [](const QString& msg) { spdlog::info(msg); });

    // 检测开始：禁用开始按钮，启用停止按钮
    connect(this, &AutoDetectionController::detectionStarted,
            this, [=](int totalCount) {
        startBtn->setEnabled(false);
        stopBtn->setEnabled(true);
        statusBar->showMessage(QString("批量检测中... 0/%1").arg(totalCount));
    });

    // 检测完成：启用开始按钮，禁用停止按钮
    connect(this, &AutoDetectionController::detectionFinished,
            this, [=](const DetectionStats& stats) {
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        statusBar->showMessage(
            QString("检测完成 | 总计:%1 合格:%2 不合格:%3 合格率:%4%")
                .arg(stats.totalCount).arg(stats.passCount)
                .arg(stats.failCount).arg(stats.passRate(), 0, 'f', 1), 10000);
    });

    // 检测停止
    connect(this, &AutoDetectionController::detectionStopped,
            this, [=]() {
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        statusBar->showMessage("检测已停止", 5000);
    });

    // 进度更新
    connect(this, &AutoDetectionController::progressUpdated,
            this, [=](int current, int total) {
        statusBar->showMessage(QString("批量检测中... %1/%2").arg(current).arg(total));
    });

    // 统计更新
    connect(this, &AutoDetectionController::statsUpdated,
            this, [=](const DetectionStats& stats) {
        QString statusText = QString("已处理: %1 | 合格: %2 | 不合格: %3 | 合格率: %4%")
            .arg(stats.processedCount).arg(stats.passCount)
            .arg(stats.failCount).arg(stats.passRate(), 0, 'f', 1);
        statusLabel->setText(statusText);
    });
}

