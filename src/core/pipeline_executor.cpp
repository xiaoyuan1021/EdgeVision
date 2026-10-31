#include "pipeline_executor.h"
#include "pipeline_steps.h"
#include "barcode_step.h"
#include "ocr_step.h"
#include <stdexcept>
#include "algorithm/object_detection_service.h"

namespace {
struct WorkerPipeline {
    ImageProcessor processor;
    Pipeline pipeline;

    WorkerPipeline() {
        pipeline.add(std::make_unique<StepColorChannel>());
        pipeline.add(std::make_unique<StepEnhance>(&processor));
        pipeline.add(std::make_unique<StepFilter>(&processor));
        pipeline.add(std::make_unique<StepAlgorithmQueue>(&processor));
        pipeline.add(std::make_unique<StepShapeFilter>());
        pipeline.add(std::make_unique<StepLineDetector>());
        pipeline.add(std::make_unique<StepBarcodeRecognition>());
        pipeline.add(std::make_unique<StepImageFilter>());
        pipeline.add(std::make_unique<StepOcrRecognition>());
    }
};
}

PipelineContext PipelineExecutor::preprocess(const cv::Mat& image, const PipelineConfig& config)
{
    if (image.empty()) throw std::invalid_argument("Pipeline input image is empty");
    thread_local WorkerPipeline worker;
    PipelineContext ctx;
    ctx.config = std::make_shared<const PipelineConfig>(config);
    ctx.srcBgr = image.clone();
    ctx.visualBase = ctx.srcBgr;
    worker.pipeline.run(ctx);
    return ctx;
}

PipelineContext PipelineExecutor::execute(const cv::Mat& image, const PipelineConfig& config)
{
    auto ctx = preprocess(image, config);
    if (ctx.pass && config.objectDetectionApplyEnabled) {
        ctx.objectDetectionResults = ObjectDetectionService::detect(ctx.srcBgr, config.objectDetection);
        ctx.objectDetectionExecuted = true;
    }
    return ctx;
}
