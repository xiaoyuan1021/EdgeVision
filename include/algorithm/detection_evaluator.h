#pragma once

#include "config/roi_config.h"
#include "config/detection_config_types.h"
#include "core/pipeline.h"
#include "data/roi_detection_result.h"
#include <functional>

// Pure per-item judgments; ROI execution is coordinated by RoiInspectionRunner.
class DetectionEvaluator
{
public:
    using Detect = std::function<std::vector<DetectionResult>(const cv::Mat&, const ObjectDetectionConfig&)>;
    /**
     * 评估单个检测项
     * @param detItem 检测项配置
     * @param ctx Pipeline执行结果（单ROI）
     * @param roiImage ROI裁剪图像（目标检测需要）
     * @param detect 独立推理函数，接收图像和检测项参数快照
     * @return 检测项评估结果
     */
    static DetectionItemResult evaluateItem(
        const DetectionItem& detItem,
        const PipelineContext& ctx,
        const cv::Mat& roiImage,
        const Detect& detect);

private:
    static DetectionItemResult evaluateBlob(
        const DetectionItem& detItem,
        const PipelineContext& ctx);

    static DetectionItemResult evaluateBarcode(
        const DetectionItem& detItem,
        const PipelineContext& ctx);

    static DetectionItemResult evaluateLine(
        const DetectionItem& detItem,
        const PipelineContext& ctx);

    static DetectionItemResult evaluateObjectDetection(
        const DetectionItem& detItem,
        const cv::Mat& roiImage,
        const Detect& detect);

    static DetectionItemResult evaluateOcr(
        const DetectionItem& detItem,
        const PipelineContext& ctx);
};
