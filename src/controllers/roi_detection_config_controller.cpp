#include "controllers/roi_detection_config_controller.h"
#include "logger.h"

RoiDetectionConfigController::RoiDetectionConfigController(
    RoiManager& roiManager,
    QObject* parent)
    : QObject(parent)
    , m_roiManager(roiManager)
{
}

void RoiDetectionConfigController::updateBlobDetectionConfig(int minCount, int maxCount)
{
    if (auto* selected = selectedItem(DetectionType::Blob)) {
            auto& detItem = *selected;
            BlobAnalysisConfig blobConfig;
            blobConfig.fromJson(detItem.config);
            blobConfig.minBlobCount = minCount;
            blobConfig.maxBlobCount = maxCount;
            detItem.config = blobConfig.toJson();
            spdlog::info(QString("[RoiDetectionConfig] 判定阈值已更新: min=%1, max=%2").arg(minCount).arg(maxCount));
    }
}

void RoiDetectionConfigController::updateOcrDetectionConfig(const QString& expectedText, bool matchExact)
{
    if (auto* selected = selectedItem(DetectionType::Ocr)) {
            auto& detItem = *selected;
            OcrDetectionConfig ocrConfig;
            ocrConfig.fromJson(detItem.config);
            ocrConfig.expectedText = expectedText;
            ocrConfig.matchExact = matchExact;
            detItem.config = ocrConfig.toJson();
            spdlog::info(QString("[RoiDetectionConfig] OCR判定参数已更新: expectedText='%1', matchExact=%2").arg(expectedText).arg(matchExact));
    }
}

void RoiDetectionConfigController::updateObjectDetectionConfig(const ObjectDetectionConfig& objConfig)
{
    if (auto* selected = selectedItem(DetectionType::ObjectDetection)) {
            auto& detItem = *selected;
            detItem.config = objConfig.toJson();
            spdlog::info(QString("[RoiDetectionConfig] 目标检测配置已更新: model=%1").arg(objConfig.modelPath));
    }
}

void RoiDetectionConfigController::updateBarcodeDetectionConfig(const BarcodeConfig& barcodeConfig)
{
    if (auto* selected = selectedItem(DetectionType::Barcode)) {
            auto& detItem = *selected;
            detItem.config = barcodeConfig.toJson();
            spdlog::info(QString("[RoiDetectionConfig] 条码检测配置已更新"));
    }
}

void RoiDetectionConfigController::updateLineDetectionConfig(const LineDetectConfig& lineConfig)
{
    if (auto* selected = selectedItem(DetectionType::Line)) {
            auto& detItem = *selected;
            detItem.config = lineConfig.toJson();
            spdlog::info(QString("[RoiDetectionConfig] 直线检测配置已更新"));
    }
}


DetectionItem* RoiDetectionConfigController::selectedItem(DetectionType type)
{
    auto* roi = m_roiManager.getRoiConfig(m_currentRoiId);
    if (!roi) return nullptr;
    DetectionItem* match = nullptr;
    for (auto& item : roi->detectionItems) {
        if (!m_currentDetectionId.isEmpty()) {
            if (item.itemId == m_currentDetectionId) return item.type == type ? &item : nullptr;
        } else if (item.type == type && item.enabled) {
            if (match) return nullptr;
            match = &item;
        }
    }
    return match;
}
