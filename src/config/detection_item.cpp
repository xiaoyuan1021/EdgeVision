#include "config/detection_item.h"

QJsonObject DetectionItem::toJson() const {
    QJsonObject obj;
    obj["itemId"] = itemId;
    obj["itemName"] = itemName;
    obj["type"] = detectionTypeToString(type);
    obj["enabled"] = enabled;
    obj["description"] = description;
    
    obj["config"] = config;
    if (pipelineSnapshot) obj["pipelineSnapshot"] = pipelineSnapshot->toJson();
    
    return obj;
}

void DetectionItem::fromJson(const QJsonObject& obj) {
    itemId = obj["itemId"].toString();
    itemName = obj["itemName"].toString();
    type = stringToDetectionType(obj["type"].toString());
    enabled = obj["enabled"].toBool(true);
    description = obj["description"].toString();
    
    config = obj["config"].toObject();
    pipelineSnapshot.reset();
    if (obj["pipelineSnapshot"].isObject()) {
        pipelineSnapshot.emplace();
        pipelineSnapshot->fromJson(obj["pipelineSnapshot"].toObject());
    }
}
PipelineConfig DetectionItem::resolvedPipeline(const PipelineConfig& fallback) const
{
    auto result = pipelineSnapshot.value_or(fallback);
    result.objectDetectionApplyEnabled = false;
    if (!pipelineSnapshot && type != DetectionType::Custom) {
        result.stepEnabled.fill(false);
        const auto enable = [&](std::initializer_list<StepType> steps) {
            for (auto step : steps) result.stepEnabled[static_cast<int>(step)] = true;
        };
        switch (type) {
        case DetectionType::Blob:
            enable({StepType::ColorChannel, StepType::Enhance, StepType::Filter,
                    StepType::AlgorithmQueue, StepType::ShapeFilter});
            break;
        case DetectionType::Line:
            enable({StepType::ColorChannel, StepType::Enhance, StepType::LineDetector}); break;
        case DetectionType::Barcode:
            enable({StepType::ColorChannel, StepType::Enhance, StepType::BarcodeRecognition}); break;
        case DetectionType::Ocr:
            enable({StepType::ColorChannel, StepType::Enhance, StepType::OcrRecognition}); break;
        default: break;
        }
    }
    // Type-specific editors remain authoritative for the fields they own.
    switch (type) {
    case DetectionType::Blob: {
        BlobAnalysisConfig blob; blob.fromJson(config);
        result.judge.minRegionCount = blob.minBlobCount;
        result.judge.maxRegionCount = blob.maxBlobCount;
        break;
    }
    case DetectionType::Line:
        result.lineDetect = {}; result.lineDetect.fromJson(config);
        result.enhance = result.lineDetect.enhance;
        break;
    case DetectionType::Barcode:
        result.barcode = {}; result.barcode.fromJson(config);
        result.enhance = result.barcode.enhance;
        break;
    case DetectionType::Ocr: {
        OcrDetectionConfig ocr; ocr.fromJson(config);
        result.ocr = ocr.ocr; result.enhance = ocr.enhance;
        break;
    }
    case DetectionType::ObjectDetection:
        result.objectDetection = {}; result.objectDetection.fromJson(config); break;
    default: break;
    }
    return result;
}

void DetectionItem::savePipeline(const PipelineConfig& pipeline)
{
    pipelineSnapshot = pipeline;
    pipelineSnapshot->objectDetectionApplyEnabled = false;
    switch (type) {
    case DetectionType::Blob: {
        BlobAnalysisConfig blob; blob.fromJson(config);
        blob.minBlobCount = pipeline.judge.minRegionCount;
        blob.maxBlobCount = pipeline.judge.maxRegionCount;
        blob.enhance = pipeline.enhance;
        config = blob.toJson();
        break;
    }
    case DetectionType::Line: {
        auto line = pipeline.lineDetect; line.enhance = pipeline.enhance;
        config = line.toJson(); break;
    }
    case DetectionType::Barcode: {
        auto barcode = pipeline.barcode; barcode.enhance = pipeline.enhance;
        config = barcode.toJson(); break;
    }
    case DetectionType::Ocr: {
        OcrDetectionConfig ocr; ocr.fromJson(config);
        ocr.ocr = pipeline.ocr; ocr.enhance = pipeline.enhance;
        config = ocr.toJson(); break;
    }
    case DetectionType::ObjectDetection: config = pipeline.objectDetection.toJson(); break;
    default: break;
    }
}
