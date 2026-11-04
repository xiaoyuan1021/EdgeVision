#pragma once
#include "algorithm/detection_evaluator.h"

class RoiInspectionRunner
{
public:
    using Preprocess = std::function<PipelineContext(const cv::Mat&, const PipelineConfig&)>;
    static RoiDetectionResult run(const RoiConfig& roi, const cv::Mat& image,
                                  const Preprocess& preprocess, const DetectionEvaluator::Detect& detect);
};
