#include <QtTest>
#include "config/roi_config.h"

class ConfigTest : public QObject {
    Q_OBJECT
private slots:
    void detectionSnapshotRoundTripPreservesIndependentPreprocessing() {
        DetectionItem item("line", DetectionType::Line);
        PipelineConfig config;
        config.enhance.brightness = 42;
        config.lineDetect.threshold = 83;
        config.lineDetect.referenceLineValid = true;
        config.lineDetect.referenceLineStart = {0, 0};
        config.lineDetect.referenceLineEnd = {40, 70};
        config.stepEnabled.fill(false);
        config.stepEnabled[static_cast<int>(StepType::LineDetector)] = true;
        item.savePipeline(config);
        DetectionItem restored;
        restored.fromJson(item.toJson());
        auto resolved = restored.resolvedPipeline({});
        QCOMPARE(resolved.enhance.brightness, 42);
        QCOMPARE(resolved.lineDetect.threshold, 83);
        QCOMPARE(resolved.lineDetect.referenceLineEnd, cv::Point2f(40, 70));
        QCOMPARE(resolved.stepEnabled, config.stepEnabled);
        auto edited = resolved.lineDetect;
        edited.angleThreshold = 21;
        restored.config = edited.toJson();
        QCOMPARE(restored.resolvedPipeline({}).lineDetect.angleThreshold, 21.0);
        auto legacy = item.toJson();
        legacy.remove("pipelineSnapshot");
        restored.fromJson(legacy);
        QVERIFY(!restored.pipelineSnapshot);
        QCOMPARE(restored.resolvedPipeline({}).lineDetect.threshold, 83);
    }

    void savingOcrSnapshotKeepsTextJudgment() {
        DetectionItem item("ocr", DetectionType::Ocr);
        OcrDetectionConfig judgment;
        judgment.expectedText = "ABC123";
        judgment.matchExact = true;
        item.config = judgment.toJson();
        PipelineConfig pipeline;
        pipeline.enhance.brightness = -13;
        item.savePipeline(pipeline);
        OcrDetectionConfig restored;
        restored.fromJson(item.config);
        QCOMPARE(restored.expectedText, QString("ABC123"));
        QVERIFY(restored.matchExact);
        QCOMPARE(restored.enhance.brightness, -13);
    }

    void legacyLineDefaults() {
        LineDetectConfig config;
        config.referenceLineStart = {5, 10};
        config.referenceLineEnd = {40, 70};
        config.referenceLineValid = true;
        config.fromJson({});
        QCOMPARE(config.toJson(), LineDetectConfig{}.toJson());
    }
    void enhanceRoundTrip() {
        EnhanceConfig a;
        a.brightness = -35; a.contrast = 180; a.gamma = 75; a.sharpen = 220;
        a.claheEnabled = true; a.claheClipLimit = 60; a.claheGridSize = 16;
        EnhanceConfig b; b.fromJson(a.toJson());
        QVERIFY(a == b);
        b.fromJson({});
        QVERIFY(b == EnhanceConfig{});
    }
    void pipelineBarcodeRoundTrip() {
        PipelineConfig a;
        a.barcode.codeTypes = {"QR_CODE", "CODE_128"};
        a.barcode.minConfidence = .91;
        a.barcode.timeout = 1234;
        a.barcode.enhance.brightness = 25;
        PipelineConfig b; b.fromJson(a.toJson());
        QCOMPARE(b.barcode.toJson(), a.barcode.toJson());
    }
    void legacyBarcodeTypes() {
        BarcodeConfig b;
        b.fromJson({{"codeTypes", "QR_CODE,CODE_128"}});
        QCOMPARE(b.codeTypes, QStringList({"QR_CODE", "CODE_128"}));
    }
    void pipelineLineRoundTrip() {
        PipelineConfig a;
        a.lineDetect.rho = 2.5;
        a.lineDetect.theta = .25;
        a.lineDetect.enhance.gamma = 150;
        a.lineDetect.enableReferenceLineMatch = true;
        a.lineDetect.referenceLineStart = {0, 0};
        a.lineDetect.referenceLineEnd = {40, 70};
        a.lineDetect.referenceLineValid = true;
        a.lineDetect.angleThreshold = 7;
        a.lineDetect.distanceThreshold = 12;
        a.lineDetect.searchRegionWidth = 35;
        PipelineConfig b; b.fromJson(a.toJson());
        QCOMPARE(b.lineDetect.toJson(), a.lineDetect.toJson());
    }
    void pipelineStepsAndQueue() {
        PipelineConfig a;
        a.stepEnabled[2] = true;
        std::swap(a.stepOrder[0], a.stepOrder[2]);
        AlgorithmStep step; step.name = "erosion"; step.type = "morphology";
        step.params["size"] = 5; step.enabled = false;
        a.algorithmQueue.append(step);
        PipelineConfig b;
        b.fromJson(a.toJson()); b.fromJson(a.toJson());
        QCOMPARE(b.algorithmQueue.size(), 1);
        QCOMPARE(b.algorithmQueue[0].params["size"].toInt(), 5);
        QVERIFY(!b.algorithmQueue[0].enabled);
        QVERIFY(b.stepEnabled == a.stepEnabled);
        QVERIFY(b.stepOrder == a.stepOrder);
    }
    void detectionTypes_data() {
        QTest::addColumn<int>("type");
        QTest::newRow("blob") << int(DetectionType::Blob);
        QTest::newRow("line") << int(DetectionType::Line);
        QTest::newRow("barcode") << int(DetectionType::Barcode);
        QTest::newRow("video-source") << int(DetectionType::VideoSource);
        QTest::newRow("object") << int(DetectionType::ObjectDetection);
        QTest::newRow("video-detection") << int(DetectionType::VideoDetection);
        QTest::newRow("ocr") << int(DetectionType::Ocr);
    }
    void detectionTypes() {
        QFETCH(int, type);
        DetectionItem a("inspection", static_cast<DetectionType>(type));
        a.enabled = false; a.description = "round trip";
        QVERIFY(!a.config.isEmpty());
        DetectionItem b; b.fromJson(a.toJson());
        QCOMPARE(b.toJson(), a.toJson());
    }
    void roiRoundTripAndReplacement() {
        RoiConfig a("part", QRectF(.1, .2, .3, .4));
        a.isActive = false; a.isSelected = true; a.color = "#123456";
        a.addDetectionItem(DetectionItem("code", DetectionType::Barcode));
        a.pipelineConfig.enhance.brightness = 12;
        RoiConfig b; b.fromJson(a.toJson()); b.fromJson(a.toJson());
        QCOMPARE(b.detectionItems.size(), 1);
        QCOMPARE(b.toJson(), a.toJson());
        const auto id = b.detectionItems[0].itemId;
        QVERIFY(b.getDetectionItem(id));
        QVERIFY(b.removeDetectionItem(id));
        QVERIFY(!b.getDetectionItem(id));
        QVERIFY(!b.removeDetectionItem(id));
    }
};
QTEST_GUILESS_MAIN(ConfigTest)
#include "config_test.moc"
