#include <QtTest>
#include "algorithm/object_detection_service.h"
#include <future>
#include <limits>
#include <stdexcept>

class ObjectDetectionServiceTest : public QObject {
    Q_OBJECT
private slots:
    void invalidTasksFailBeforeInference() {
        ObjectDetectionConfig config;
        QVERIFY_EXCEPTION_THROWN(ObjectDetectionService::detect({}, config), std::invalid_argument);
        cv::Mat image(16, 16, CV_8UC3, cv::Scalar(114, 114, 114));
        config.confidenceThreshold = std::numeric_limits<float>::quiet_NaN();
        QVERIFY_EXCEPTION_THROWN(ObjectDetectionService::detect(image, config), std::invalid_argument);
        config.confidenceThreshold = .5f;
        config.modelPath = "missing-model-for-test.onnx";
        QVERIFY_EXCEPTION_THROWN(ObjectDetectionService::detect(image, config), std::runtime_error);
    }
    void realModelRunsWithoutWidgets() {
        ObjectDetectionConfig config;
        config.modelPath = ObjectDetectionService::defaultModelPath();
        QVERIFY2(QFileInfo::exists(config.modelPath), "Bundled pin model is required for this integration test");
        auto info = ObjectDetectionService::prepare(config);
        QVERIFY(!info.backend.isEmpty());
        cv::Mat image(72, 128, CV_8UC3, cv::Scalar(114, 114, 114));
        auto results = ObjectDetectionService::detect(image, config);
        for (const auto& result : results) {
            QVERIFY(result.confidence >= config.confidenceThreshold);
            QVERIFY(result.box.x >= 0 && result.box.y >= 0);
            QVERIFY(result.box.br().x <= image.cols && result.box.br().y <= image.rows);
        }
        auto bad = config;
        bad.modelPath = "missing-new-model.onnx";
        QVERIFY_EXCEPTION_THROWN(ObjectDetectionService::detect(image, bad), std::runtime_error);
        // Failed replacement must neither use the stale model nor corrupt it.
        QCOMPARE(ObjectDetectionService::detect(image, config).size(), results.size());
    }
    void independentWorkersUseTheirOwnParameters() {
        auto run = [](float threshold) {
            ObjectDetectionConfig config;
            config.confidenceThreshold = threshold;
            cv::Mat image(48, 80, CV_8UC3, cv::Scalar(0, 0, 0));
            auto results = ObjectDetectionService::detect(image, config);
            return std::all_of(results.begin(), results.end(), [=](const auto& result) {
                return result.confidence >= threshold;
            });
        };
        auto a = std::async(std::launch::async, run, .95f);
        auto b = std::async(std::launch::async, run, .55f);
        QVERIFY(a.get());
        QVERIFY(b.get());
    }
};
QTEST_GUILESS_MAIN(ObjectDetectionServiceTest)
#include "object_detection_service_test.moc"
