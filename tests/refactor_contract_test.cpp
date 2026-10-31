#include <QtTest>
#include <QPushButton>
#include <QSpinBox>
#include "widgets/barcode_tab_widget.h"
#include "algorithm/detection_evaluator.h"
#include <stdexcept>

#include "pipeline_access_stub.h"

class RefactorContractTest : public QObject {
    Q_OBJECT
private slots:
    void barcodeRenderEditApplyAreDistinct() {
        PipelineStub pipeline;
        pipeline.value.enhance.brightness = 37;
        BarcodeTabWidget widget(&pipeline);
        int executions = 0, saved = 0, edits = 0;
        BarcodeConfig edited;
        SignalContext wiring{nullptr, nullptr, nullptr, nullptr,
            [&](const BarcodeConfig& config) { ++edits; edited = config; }};
        widget.connectSignals(wiring, [&] { ++executions; }, [&] { ++saved; });
        widget.initializeTab({});
        BarcodeConfig config;
        config.enableBarcode = true;
        config.codeTypes = {"QR_CODE", "CODE_128"};
        config.timeout = 1234;
        config.minConfidence = .93;
        config.enhance.brightness = 41;
        config.maxNumSymbols = 2;
        widget.setBarcodeConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QCOMPARE(edits, 0);
        QCOMPARE(executions, 0);
        QCOMPARE(saved, 0);
        QCOMPARE(widget.getBarcodeConfig().toJson(), config.toJson());
        auto* maxCount = widget.findChild<QSpinBox*>("spinBox_maxNum");
        auto* apply = widget.findChild<QPushButton*>("btn_apply");
        QVERIFY(maxCount && apply);
        maxCount->setValue(4);
        QCOMPARE(edits, 1);
        QCOMPARE(pipeline.writes, 0);
        QCOMPARE(executions, 0);
        config.maxNumSymbols = 4;
        QCOMPARE(edited.toJson(), config.toJson());
        apply->click();
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(executions, 1);
        QCOMPARE(saved, 1);
        QCOMPARE(pipeline.value.barcode.toJson(), config.toJson());
        QCOMPARE(pipeline.value.enhance.brightness, 37);
        config.codeTypes = {"auto"};
        config.timeout = 7654;
        widget.setBarcodeConfig(config); // Simulate a second ROI being selected.
        QCOMPARE(edits, 1);
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(executions, 1);
        maxCount->setValue(5);
        QCOMPARE(edited.timeout, 7654);
        QCOMPARE(edited.codeTypes, QStringList{"auto"});
    }
    void detectorReceivesItemSnapshotWithoutWidgets() {
        DetectionItem item;
        item.type = DetectionType::ObjectDetection;
        ObjectDetectionConfig config;
        config.modelPath = "per-item-model.onnx";
        config.confidenceThreshold = .83f;
        config.expectedCount = 2;
        item.config = config.toJson();
        ObjectDetectionConfig received;
        auto result = DetectionEvaluator::evaluateItem(item, {}, {},
            [&](const cv::Mat&, const ObjectDetectionConfig& value) {
                received = value;
                return std::vector<DetectionResult>(2);
            });
        QVERIFY(result.passed);
        QCOMPARE(received.toJson(), config.toJson());
        auto mismatch = DetectionEvaluator::evaluateItem(item, {}, {},
            [](const cv::Mat&, const ObjectDetectionConfig&) { return std::vector<DetectionResult>(1); });
        QVERIFY(!mismatch.passed);
    }
    void modelFailureCannotPassAnUncheckedCount() {
        DetectionItem item;
        item.type = DetectionType::ObjectDetection;
        item.config = ObjectDetectionConfig{}.toJson(); // expectedCount = 0
        int calls = 0;
        DetectionEvaluator::Detect fail = [&](const cv::Mat&, const ObjectDetectionConfig&) -> std::vector<DetectionResult> {
            ++calls;
            throw std::runtime_error("model unavailable");
        };
        auto result = DetectionEvaluator::evaluateItem(item, {}, {}, fail);
        QVERIFY(!result.passed);
        QVERIFY(result.failReason.contains("model unavailable"));
        item.enabled = false;
        QVERIFY(DetectionEvaluator::evaluateItem(item, {}, {}, fail).passed);
        QCOMPARE(calls, 1);
    }
};
QTEST_MAIN(RefactorContractTest)
#include "refactor_contract_test.moc"
