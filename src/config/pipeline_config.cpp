#include "config/pipeline_config.h"
#include <QJsonArray>
#include <cmath>

QJsonObject PipelineConfig::toJson() const {
    QJsonObject obj;

    // 增强参数（现在都是 int 原始值）
    obj["enhance"] = enhance.toJson();

    // 通道/过滤参数
    QJsonObject colorObj;
    colorObj["channel"] = static_cast<int>(colorFilter.channel);
    colorObj["filterMode"] = static_cast<int>(colorFilter.mode);
    colorObj["grayLow"] = colorFilter.grayLow;
    colorObj["grayHigh"] = colorFilter.grayHigh;
    colorObj["rLow"] = colorFilter.rLow;
    colorObj["rHigh"] = colorFilter.rHigh;
    colorObj["gLow"] = colorFilter.gLow;
    colorObj["gHigh"] = colorFilter.gHigh;
    colorObj["bLow"] = colorFilter.bLow;
    colorObj["bHigh"] = colorFilter.bHigh;
    colorObj["hLow"] = colorFilter.hLow;
    colorObj["hHigh"] = colorFilter.hHigh;
    colorObj["sLow"] = colorFilter.sLow;
    colorObj["sHigh"] = colorFilter.sHigh;
    colorObj["vLow"] = colorFilter.vLow;
    colorObj["vHigh"] = colorFilter.vHigh;
    obj["colorFilter"] = colorObj;

    // 直线检测参数
    obj["lineDetect"] = lineDetect.toJson();

    // 条码参数
    obj["barcode"] = barcode.toJson();

    // 滤波去噪参数
    obj["imageFilter"] = imageFilter.toJson();

    // OCR参数
    obj["ocr"] = ocr.toJson();

    // 目标检测参数
    obj["objectDetection"] = objectDetection.toJson();

    // 步骤控制
    QJsonArray enabledArr, orderArr;
    for (int i = 0; i < PipelineConfig::STEP_COUNT; ++i) {
        enabledArr.append(stepEnabled[i]);
        orderArr.append(stepOrder[i]);
    }
    obj["stepEnabled"] = enabledArr;
    obj["stepOrder"] = orderArr;
    obj["enableObjectDetection"] = enableObjectDetection;
    obj["objectDetectionApplyEnabled"] = objectDetectionApplyEnabled;

    // 形状筛选参数
    obj["shapeFilter"] = shapeFilter.toJson();

    // 算法队列
    QJsonArray algoArr;
    for (const auto& step : algorithmQueue) {
        QJsonObject algoObj;
        algoObj["name"] = step.name;
        algoObj["type"] = step.type;
        algoObj["enabled"] = step.enabled;
        algoObj["description"] = step.description;
        QJsonObject paramsObj;
        for (auto it = step.params.constBegin(); it != step.params.constEnd(); ++it) {
            paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        algoObj["params"] = paramsObj;
        algoArr.append(algoObj);
    }
    obj["algorithmQueue"] = algoArr;

    return obj;
}

void PipelineConfig::resetToDefaults()
{
    *this = PipelineConfig();
}

void PipelineConfig::fromJson(const QJsonObject& obj) {
    if (obj.isEmpty()) return;

    // 增强参数
    if (obj.contains("enhance")) {
        enhance.fromJson(obj["enhance"].toObject());
    }

    // 通道/过滤参数
    if (obj.contains("colorFilter")) {
        QJsonObject colorObj = obj["colorFilter"].toObject();
        colorFilter.channel = static_cast<ChannelMode>(colorObj["channel"].toInt(0));
        colorFilter.mode = static_cast<ImageFilterMode>(colorObj["filterMode"].toInt(0));
        colorFilter.grayLow = colorObj["grayLow"].toInt(0);
        colorFilter.grayHigh = colorObj["grayHigh"].toInt(255);
        colorFilter.rLow = colorObj["rLow"].toInt(0);
        colorFilter.rHigh = colorObj["rHigh"].toInt(255);
        colorFilter.gLow = colorObj["gLow"].toInt(0);
        colorFilter.gHigh = colorObj["gHigh"].toInt(255);
        colorFilter.bLow = colorObj["bLow"].toInt(0);
        colorFilter.bHigh = colorObj["bHigh"].toInt(255);
        colorFilter.hLow = colorObj["hLow"].toInt(0);
        colorFilter.hHigh = colorObj["hHigh"].toInt(179);
        colorFilter.sLow = colorObj["sLow"].toInt(0);
        colorFilter.sHigh = colorObj["sHigh"].toInt(255);
        colorFilter.vLow = colorObj["vLow"].toInt(0);
        colorFilter.vHigh = colorObj["vHigh"].toInt(255);
    }

    // 直线检测参数
    if (obj.contains("lineDetect")) {
        lineDetect.fromJson(obj["lineDetect"].toObject());
    }

    // 条码参数
    QJsonObject barcodeObj = obj["barcode"].toObject();
    if (!barcodeObj.isEmpty()) {
        barcode.fromJson(barcodeObj);
    }

    // 滤波去噪参数
    if (obj.contains("imageFilter")) {
        imageFilter.fromJson(obj["imageFilter"].toObject());
    }

    // OCR参数
    if (obj.contains("ocr")) {
        ocr.fromJson(obj["ocr"].toObject());
    }

    // 目标检测参数
    if (obj.contains("objectDetection")) {
        objectDetection.fromJson(obj["objectDetection"].toObject());
    }

    // 步骤控制
    QJsonArray enabledArr = obj["stepEnabled"].toArray();
    if (enabledArr.size() == PipelineConfig::STEP_COUNT) {
        for (int i = 0; i < PipelineConfig::STEP_COUNT; ++i) {
            stepEnabled[i] = enabledArr[i].toBool(true);
        }
    }
    QJsonArray orderArr = obj["stepOrder"].toArray();
    if (orderArr.size() == PipelineConfig::STEP_COUNT) {
        for (int i = 0; i < PipelineConfig::STEP_COUNT; ++i) {
            stepOrder[i] = orderArr[i].toInt(i);
        }
    }

    enableObjectDetection = obj["enableObjectDetection"].toBool(false);
    objectDetectionApplyEnabled = obj["objectDetectionApplyEnabled"].toBool(false);

    // 形状筛选参数
    if (obj.contains("shapeFilter")) {
        shapeFilter.fromJson(obj["shapeFilter"].toObject());
    }

    // 算法队列
    algorithmQueue.clear();
    QJsonArray algoArr = obj["algorithmQueue"].toArray();
    for (const auto& val : algoArr) {
        QJsonObject algoObj = val.toObject();
        AlgorithmStep step;
        step.name = algoObj["name"].toString();
        step.type = algoObj["type"].toString();
        step.enabled = algoObj["enabled"].toBool(true);
        step.description = algoObj["description"].toString();
        QJsonObject paramsObj = algoObj["params"].toObject();
        for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it) {
            step.params[it.key()] = it.value().toVariant();
        }
        algorithmQueue.append(step);
    }
}

// ========== LineDetectConfig 序列化 ==========

QJsonObject LineDetectConfig::toJson() const {
    QJsonObject obj;
    obj["enhance"] = enhance.toJson();
    obj["algorithm"] = algorithm;
    obj["rho"] = rho;
    obj["theta"] = theta;
    obj["threshold"] = threshold;
    obj["minLength"] = minLength;
    obj["maxGap"] = maxGap;
    obj["enabled"] = enabled;
    obj["enableReferenceLineMatch"] = enableReferenceLineMatch;
    obj["referenceLineStart"] = QJsonArray{referenceLineStart.x, referenceLineStart.y};
    obj["referenceLineEnd"] = QJsonArray{referenceLineEnd.x, referenceLineEnd.y};
    obj["referenceLineValid"] = referenceLineValid;
    obj["angleThreshold"] = angleThreshold;
    obj["distanceThreshold"] = distanceThreshold;
    obj["searchRegionWidth"] = searchRegionWidth;
    return obj;
}

void LineDetectConfig::fromJson(const QJsonObject& obj) {
    if (obj.contains("enhance")) {
        enhance.fromJson(obj["enhance"].toObject());
    }
    algorithm = obj["algorithm"].toInt(0);
    rho = obj["rho"].toDouble(1.0);
    theta = obj["theta"].toDouble(CV_PI / 180.0);
    threshold = obj["threshold"].toInt(50);
    minLength = obj["minLength"].toDouble(30.0);
    maxGap = obj["maxGap"].toDouble(10.0);
    enabled = obj["enabled"].toBool(false);
    enableReferenceLineMatch = obj["enableReferenceLineMatch"].toBool(false);
    const auto start = obj["referenceLineStart"].toArray();
    const auto end = obj["referenceLineEnd"].toArray();
    referenceLineStart = {};
    referenceLineEnd = {};
    referenceLineValid = false;
    if (start.size() == 2 && end.size() == 2 && start[0].isDouble() && start[1].isDouble()
        && end[0].isDouble() && end[1].isDouble()) {
        referenceLineStart = {float(start[0].toDouble()), float(start[1].toDouble())};
        referenceLineEnd = {float(end[0].toDouble()), float(end[1].toDouble())};
        referenceLineValid = obj["referenceLineValid"].toBool(false)
            && std::isfinite(referenceLineStart.x) && std::isfinite(referenceLineStart.y)
            && std::isfinite(referenceLineEnd.x) && std::isfinite(referenceLineEnd.y)
            && referenceLineStart != referenceLineEnd;
    }
    angleThreshold = obj["angleThreshold"].toDouble(LineDetectConfig{}.angleThreshold);
    distanceThreshold = obj["distanceThreshold"].toDouble(LineDetectConfig{}.distanceThreshold);
    searchRegionWidth = obj["searchRegionWidth"].toInt(LineDetectConfig{}.searchRegionWidth);
}

// ========== BarcodeConfig 序列化 ==========

QJsonObject BarcodeConfig::toJson() const {
    QJsonObject obj;
    obj["enhance"] = enhance.toJson();
    obj["enableBarcode"] = enableBarcode;
    QJsonArray codeTypesArray;
    for (const QString& type : codeTypes) {
        codeTypesArray.append(type);
    }
    obj["codeTypes"] = codeTypesArray;
    obj["maxNumSymbols"] = maxNumSymbols;
    obj["returnQuality"] = returnQuality;
    obj["minConfidence"] = minConfidence;
    obj["timeout"] = timeout;
    obj["enablePreprocessing"] = enablePreprocessing;
    obj["preprocessMethod"] = preprocessMethod;
    obj["binarizationThreshold"] = binarizationThreshold;
    obj["morphologySize"] = morphologySize;
    return obj;
}

void BarcodeConfig::fromJson(const QJsonObject& obj) {
    if (obj.contains("enhance")) {
        enhance.fromJson(obj["enhance"].toObject());
    }
    enableBarcode = obj["enableBarcode"].toBool(true);
    codeTypes.clear();
    if (obj["codeTypes"].isString()) {
        codeTypes = obj["codeTypes"].toString().split(',', Qt::SkipEmptyParts);
    }
    QJsonArray codeTypesArray = obj["codeTypes"].toArray();
    for (const QJsonValue& val : codeTypesArray) {
        codeTypes.append(val.toString());
    }
    if (codeTypes.isEmpty()) {
        codeTypes = {"auto"};
    }
    maxNumSymbols = obj["maxNumSymbols"].toInt(0);
    returnQuality = obj["returnQuality"].toBool(true);
    minConfidence = obj["minConfidence"].toDouble(0.7);
    timeout = obj["timeout"].toInt(5000);
    enablePreprocessing = obj["enablePreprocessing"].toBool(true);
    preprocessMethod = obj["preprocessMethod"].toInt(0);
    binarizationThreshold = obj["binarizationThreshold"].toInt(128);
    morphologySize = obj["morphologySize"].toInt(3);
}
