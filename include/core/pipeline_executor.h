#pragma once

#include "pipeline.h"

// Executes snapshots without accessing a manager or a widget. Stateful algorithms
// (notably OCR) belong to the calling worker thread and are reused there.
class PipelineExecutor
{
public:
    // Batch detection evaluates each item with its own model/config snapshot.
    static PipelineContext preprocess(const cv::Mat& image, const PipelineConfig& config);
    static PipelineContext execute(const cv::Mat& image, const PipelineConfig& config);
};
