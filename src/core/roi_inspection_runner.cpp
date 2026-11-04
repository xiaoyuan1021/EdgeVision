#include "core/roi_inspection_runner.h"
#include <stdexcept>

RoiDetectionResult RoiInspectionRunner::run(const RoiConfig& roi, const cv::Mat& image,
    const Preprocess& preprocess, const DetectionEvaluator::Detect& detect)
{
    RoiDetectionResult result(roi.roiId, roi.roiName, {});
    const auto inspect = [&](const DetectionItem& item) {
        DetectionItemResult judgment(item.itemId, item.itemName, detectionTypeToString(item.type));
        try {
            if (!preprocess) throw std::runtime_error("No pipeline executor configured");
            const auto context = preprocess(image, item.resolvedPipeline(roi.pipelineConfig));
            result.regionCount += context.regionCount;
            result.regionFeatures.insert(result.regionFeatures.end(),
                context.regionFeatures.begin(), context.regionFeatures.end());
            result.barcodeResults += context.barcodeResults;
            result.ocrRegions += context.ocrRegions;
            if (!context.ocrText.isEmpty()) {
                if (!result.ocrText.isEmpty()) result.ocrText += "\n";
                result.ocrText += context.ocrText;
            }
            result.matchedLineCount += context.matchedLineCount;
            result.totalLineCount += context.totalLineCount;
            if (context.pass) judgment = DetectionEvaluator::evaluateItem(item, context, image, detect);
            else {
                judgment.passed = false;
                judgment.failReason = context.reason.isEmpty() ? QStringLiteral("Pipeline执行失败") : context.reason;
            }
        } catch (const std::exception& error) {
            judgment.passed = false;
            judgment.failReason = QString::fromUtf8(error.what());
        }
        result.addItemResult(judgment);
    };

    if (roi.detectionItems.isEmpty()) {
        // Legacy ROIs can contain only a pipeline and no explicit judgments.
        DetectionItem pipeline(QStringLiteral("Pipeline执行"), DetectionType::Custom);
        pipeline.itemId = "pipeline";
        inspect(pipeline);
    } else {
        for (const auto& item : roi.detectionItems)
            if (item.enabled) inspect(item);
    }
    return result;
}
