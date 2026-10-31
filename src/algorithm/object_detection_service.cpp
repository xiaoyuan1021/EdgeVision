#include "algorithm/object_detection_service.h"
#include "algorithm/ort_inference.h"
#include "algorithm/dnn_inference.h"
#include <QFileInfo>
#include <cmath>
#include <stdexcept>

namespace {
struct Model {
    OrtInference ort;
    DnnInference dnn;
    QString path;
    QString configPath;
    DetectionModelInfo info;
};
thread_local std::unique_ptr<Model> model;

Model& ensureModel(const ObjectDetectionConfig& config) {
    const QString path = QFileInfo(config.modelPath.trimmed().isEmpty()
        ? ObjectDetectionService::defaultModelPath() : config.modelPath.trimmed()).absoluteFilePath();
    const QString configPath = config.configPath.trimmed();
    if (model && model->path == path && model->configPath == configPath) return *model;
    if (!QFileInfo::exists(path)) throw std::runtime_error(("Model not found: " + path).toStdString());

    // Replace only after a complete successful load; a failed new model is never
    // silently served by the old model retained in the worker.
    auto next = std::make_unique<Model>();
    next->path = path;
    next->configPath = configPath;
    if (configPath.isEmpty() && next->ort.loadModel(path, true)) {
        next->info = {next->ort.isUsingGpu() ? "ORT CUDA" : "ORT CPU",
                      next->ort.getModelImgWidth(), next->ort.getModelImgHeight()};
    } else if (next->dnn.loadModel(path, configPath)) {
        const auto size = next->dnn.getModelInputSize();
        next->info = {"OpenCV DNN", size.width, size.height};
    } else {
        throw std::runtime_error(("Unable to load detection model: " + path).toStdString());
    }
    model = std::move(next);
    return *model;
}
}

QString ObjectDetectionService::defaultModelPath() {
    return QStringLiteral(PROJECT_ROOT_DIR) + "/resources/models/model_pin/ort/pin.onnx";
}

DetectionModelInfo ObjectDetectionService::prepare(const ObjectDetectionConfig& config) {
    return ensureModel(config).info;
}

std::vector<DetectionResult> ObjectDetectionService::detect(const cv::Mat& image,
                                                           const ObjectDetectionConfig& config) {
    if (image.empty() || image.type() != CV_8UC3)
        throw std::invalid_argument("Object detection requires a nonempty BGR8 image");
    if (!std::isfinite(config.confidenceThreshold) || config.confidenceThreshold < 0 || config.confidenceThreshold > 1
        || !std::isfinite(config.nmsThreshold) || config.nmsThreshold < 0 || config.nmsThreshold > 1)
        throw std::invalid_argument("Detection thresholds must be between 0 and 1");
    auto& state = ensureModel(config);
    // Fixed model dimensions take precedence, as in the previous UI inference
    // path. Dynamic models fall back to the dimensions in the task snapshot.
    const int width = state.info.inputWidth > 0 ? state.info.inputWidth : config.inputWidth;
    const int height = state.info.inputHeight > 0 ? state.info.inputHeight : config.inputHeight;
    if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid model input dimensions");
    if (state.ort.isLoaded())
        return state.ort.detect(image, config.confidenceThreshold, config.nmsThreshold, width, height);
    return state.dnn.detect(image, config.confidenceThreshold, config.nmsThreshold, width, height);
}
