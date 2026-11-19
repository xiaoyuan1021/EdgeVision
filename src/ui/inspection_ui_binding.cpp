#include "ui/inspection_ui_binding.h"
#include "ui/inspection_results_dialog.h"
#include "controllers/auto_detection_controller.h"
#include "core/roi_manager.h"
#include "core/logger.h"
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QWidget>

InspectionUiBinding::InspectionUiBinding(AutoDetectionController* detection, RoiManager* rois,
    QPushButton* start, QPushButton* stop, QPushButton* results,
    QStatusBar* status, QLabel* label, QWidget* window)
    : QObject(window), m_detection(detection), m_rois(rois), m_start(start),
      m_stop(stop), m_status(status), m_label(label), m_window(window)
{
    connect(detection, &AutoDetectionController::logMessage, this,
        [](const QString& message) { spdlog::info(message); });
    connect(detection, &AutoDetectionController::detectionStarted, this, [this](int count) {
        refreshRetryAvailability();
        if (m_start) m_start->setEnabled(false);
        if (m_stop) m_stop->setEnabled(true);
        if (m_label) m_label->clear();
        if (m_status) m_status->showMessage(QStringLiteral("批量检测中... 0/%1").arg(count));
    });
    connect(detection, &AutoDetectionController::detectionFinished, this, [this](const DetectionStats& stats) {
        refreshRetryAvailability();
        if (m_start) m_start->setEnabled(true);
        if (m_stop) m_stop->setEnabled(false);
        if (!m_status) return;
        const bool stopped = stats.processedCount < stats.totalCount;
        m_status->showMessage(
            QStringLiteral("%1 | 已处理:%2/%3 合格:%4 不合格:%5 合格率:%6%")
                .arg(stopped ? QStringLiteral("检测已停止") : QStringLiteral("检测完成"))
                .arg(stats.processedCount).arg(stats.totalCount).arg(stats.passCount)
                .arg(stats.failCount).arg(stats.passRate(), 0, 'f', 1), 10000);
    });
    connect(detection, &AutoDetectionController::detectionStopped, this, [this] {
        if (m_start) m_start->setEnabled(true);
        if (m_stop) m_stop->setEnabled(false);
        if (m_status) m_status->showMessage(QStringLiteral("检测已停止"), 5000);
    });
    connect(detection, &AutoDetectionController::progressUpdated, this, [this](int current, int total) {
        if (m_status)
            m_status->showMessage(QStringLiteral("批量检测中... %1/%2").arg(current).arg(total));
    });
    connect(detection, &AutoDetectionController::statsUpdated, this, [this](const DetectionStats& stats) {
        if (m_label)
            m_label->setText(QStringLiteral("已处理: %1 | 合格: %2 | 不合格: %3 | 合格率: %4%")
                .arg(stats.processedCount).arg(stats.passCount).arg(stats.failCount)
                .arg(stats.passRate(), 0, 'f', 1));
    });
    connect(detection, &AutoDetectionController::recordsChanged, this, &InspectionUiBinding::refreshResults);
    connect(results, &QPushButton::clicked, this, &InspectionUiBinding::showResults);
    if (m_start) m_start->setEnabled(!detection->isRunning());
    if (m_stop) m_stop->setEnabled(detection->isRunning());
}

void InspectionUiBinding::showResults()
{
    if (!m_window || !m_detection) return;
    if (!m_dialog) {
        m_dialog = new InspectionResultsDialog(m_window);
        connect(m_dialog, &InspectionResultsDialog::imageActivated, this, &InspectionUiBinding::activateImage);
        connect(m_dialog, &InspectionResultsDialog::retryFailedRequested, this, [this] {
            if (m_detection && !m_detection->retryFailed())
                QMessageBox::information(m_dialog, QStringLiteral("没有可复检的图片"),
                    QStringLiteral("请确认失败图片仍在工作区，且有启用的 ROI 和检测项。原检测记录已保留。"));
        });
    }
    refreshResults();
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
}

void InspectionUiBinding::refreshResults()
{
    if (m_dialog && m_detection) m_dialog->setRecords(m_detection->records());
    refreshRetryAvailability();
}

void InspectionUiBinding::refreshRetryAvailability()
{
    if (m_dialog)
        m_dialog->setRetryEnabled(m_detection && !m_detection->isRunning()
            && !m_detection->failedImageIds().isEmpty());
}

void InspectionUiBinding::activateImage(const QString& imageId)
{
    if (!m_rois || !m_rois->switchToImage(imageId)) {
        if (m_status)
            m_status->showMessage(QStringLiteral("对应图片已从工作区移除，检测记录仍可查看和导出"), 7000);
        return;
    }
    if (m_dialog) m_dialog->hide();
    if (m_window) {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    }
}
