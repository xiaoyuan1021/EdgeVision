#pragma once
#include "config/object_detection_config.h"
#include "data/object_detection_result.h"
#include <opencv2/core/mat.hpp>
#include <vector>

struct DetectionModelInfo {
    QString backend;
    int inputWidth = 0;
    int inputHeight = 0;
};

// No UI dependencies. Each caller thread owns its model instance. Model errors
// throw; an empty successful result means no objects met the confidence threshold.
class ObjectDetectionService {
public:
    static QString defaultModelPath();
    static DetectionModelInfo prepare(const ObjectDetectionConfig& config);
    static std::vector<DetectionResult> detect(const cv::Mat& image,
                                               const ObjectDetectionConfig& config);
};
