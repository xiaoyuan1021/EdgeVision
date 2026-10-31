#pragma once
#include <opencv2/core/types.hpp>
#include <string>

struct DetectionResult {
    cv::Rect box;
    float confidence = 0;
    int classId = -1;
    std::string className;
};
