#include "algorithm/detection_evaluator.h"
#include "config/detection_config_types.h"
#include "logger.h"
#include <stdexcept>

DetectionItemResult DetectionEvaluator::evaluateItem(
    const DetectionItem& detItem,
    const PipelineContext& ctx,
    const cv::Mat& roiImage,
    const Detect& detect)
{
    if (!detItem.enabled) {
        return DetectionItemResult(detItem.itemId, detItem.itemName, detectionTypeToString(detItem.type));
    }

    DetectionItemResult result;
    switch (detItem.type) {
        case DetectionType::Blob:
            result = evaluateBlob(detItem, ctx);
            break;
        case DetectionType::Barcode:
            result = evaluateBarcode(detItem, ctx);
            break;
        case DetectionType::Line:
            result = evaluateLine(detItem, ctx);
            break;
        case DetectionType::ObjectDetection:
            result = evaluateObjectDetection(detItem, roiImage, detect);
            break;
        case DetectionType::Ocr:
            result = evaluateOcr(detItem, ctx);
            break;
        case DetectionType::Custom:
            result.passed = ctx.pass;
            result.failReason = ctx.reason;
            break;
        default:
            result = DetectionItemResult(detItem.itemId, detItem.itemName, detectionTypeToString(detItem.type));
            result.passed = false;
            result.failReason = QStringLiteral("该检测类型不支持批量评估");
            spdlog::warn(QString("[评估] 未知检测类型: %1").arg(detectionTypeToString(detItem.type)));
            break;
    }

    result.itemId = detItem.itemId;
    result.itemName = detItem.itemName;
    result.detectionType = detectionTypeToString(detItem.type);
    return result;
}

// ==================== 私有评估方法 ====================

DetectionItemResult DetectionEvaluator::evaluateBlob(
    const DetectionItem& detItem,
    const PipelineContext& ctx)
{
    DetectionItemResult result;
    BlobAnalysisConfig blobConfig;
    blobConfig.fromJson(detItem.config);

    int currentCount = ctx.regionCount;
    int minCount = blobConfig.minBlobCount;
    int maxCount = blobConfig.maxBlobCount;

    if (currentCount < minCount || currentCount > maxCount) {
        result.passed = false;
        result.failReason = QString("Blob数量%1, 要求[%2,%3]")
            .arg(currentCount).arg(minCount).arg(maxCount);
    }

    return result;
}

DetectionItemResult DetectionEvaluator::evaluateBarcode(
    const DetectionItem& detItem,
    const PipelineContext& ctx)
{
    DetectionItemResult result;
        BarcodeConfig barcodeConfig;
    barcodeConfig.fromJson(detItem.config);

    if (ctx.barcodeResults.isEmpty()) {
        result.passed = false;
        result.failReason = "未检测到条码";
    }

    return result;
}

DetectionItemResult DetectionEvaluator::evaluateLine(
    const DetectionItem& detItem,
    const PipelineContext& ctx)
{
    DetectionItemResult result;
        LineDetectConfig lineConfig;
    lineConfig.fromJson(detItem.config);

    if (ctx.matchedLineCount == 0 && ctx.totalLineCount == 0) {
        result.passed = false;
        result.failReason = "未检测到直线";
    }

    return result;
}

DetectionItemResult DetectionEvaluator::evaluateObjectDetection(
    const DetectionItem& detItem,
    const cv::Mat& roiImage,
    const Detect& detect)
{
    DetectionItemResult result;
    ObjectDetectionConfig objConfig;
    objConfig.fromJson(detItem.config);

    int detectedCount = 0;
    try {
        if (!detect) throw std::runtime_error("No object detection service configured");
        detectedCount = static_cast<int>(detect(roiImage, objConfig).size());
    } catch (const std::exception& ex) {
        result.passed = false;
        result.failReason = QString("目标检测失败: %1").arg(ex.what());
        return result;
    }

    int expectedCount = objConfig.expectedCount;
    if (expectedCount > 0 && detectedCount != expectedCount) {
        result.passed = false;
        result.failReason = QString("检测到%1个目标, 期望%2个")
            .arg(detectedCount).arg(expectedCount);
    }

    return result;
}

DetectionItemResult DetectionEvaluator::evaluateOcr(
    const DetectionItem& detItem,
    const PipelineContext& ctx)
{
    DetectionItemResult result; 
    OcrDetectionConfig ocrConfig;
    ocrConfig.fromJson(detItem.config);

    QString recognizedText = ctx.ocrText.trimmed();
    QString expectedText = ocrConfig.expectedText.trimmed();

    if (expectedText.isEmpty()) {
        if (recognizedText.isEmpty()) {
            result.passed = false;
            result.failReason = "未识别到文字";
        }
    } else {
        bool matched = ocrConfig.matchExact
            ? (recognizedText == expectedText)
            : recognizedText.contains(expectedText);

        if (!matched) {
            result.passed = false;
            result.failReason = QString("OCR文本'%1'不匹配'%2'")
                .arg(recognizedText.isEmpty() ? "(空)" : recognizedText)
                .arg(expectedText);
        }
    }

    return result;
}

