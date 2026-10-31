#include <QtTest>
#include <QSignalSpy>
#include "core/pipeline_scheduler.h"
#include <stdexcept>

namespace {
PipelineContext echo(const cv::Mat& image, const PipelineConfig& config) {
    PipelineContext ctx;
    ctx.srcBgr = image.clone();
    ctx.config = std::make_shared<const PipelineConfig>(config);
    return ctx;
}
class RecordingStep : public IPipelineStep {
public:
    RecordingStep(StepType type, QList<int>& calls) : type(type), calls(calls) {}
    void run(PipelineContext&) override { calls.append(static_cast<int>(type)); }
    StepType stepType() const override { return type; }
private:
    StepType type;
    QList<int>& calls;
};
class FailingStep : public IPipelineStep {
public:
    void run(PipelineContext&) override { throw std::runtime_error("bad preprocessing"); }
    StepType stepType() const override { return StepType::ColorChannel; }
};
}

class PipelineRuntimeTest : public QObject {
    Q_OBJECT
private slots:
    void failedStepStopsDependentSteps() {
        Pipeline pipeline;
        QList<int> calls;
        pipeline.add(std::make_unique<FailingStep>());
        pipeline.add(std::make_unique<RecordingStep>(StepType::Enhance, calls));
        PipelineContext context;
        pipeline.run(context);
        QVERIFY(!context.pass);
        QVERIFY(context.reason.contains("bad preprocessing"));
        QVERIFY(calls.empty());
    }
    void snapshotPreservesIdentityAndOwnsPixels() {
        cv::Mat image(2, 2, CV_8UC1, cv::Scalar(7));
        PipelineConfig config;
        config.enhance.brightness = 25;
        PipelineRequest request(image, config, 3, "preview", {"image-a", "roi-b", 42});
        auto copy = request.snapshot();
        image.setTo(99);
        config.enhance.brightness = 70;
        QCOMPARE(copy.timestamp(), request.timestamp());
        QCOMPARE(copy.id(), request.id());
        QCOMPARE(copy.identity().generation, quint64(42));
        QCOMPARE(copy.identity().roiId, QString("roi-b"));
        QCOMPARE(copy.config().enhance.brightness, 25);
        QCOMPARE(copy.image().at<uchar>(0, 0), uchar(7));
        QVERIFY(copy.image().data != request.image().data);
    }
    void orderBelongsToTask() {
        Pipeline pipeline;
        QList<int> calls;
        pipeline.add(std::make_unique<RecordingStep>(StepType::ColorChannel, calls));
        pipeline.add(std::make_unique<RecordingStep>(StepType::Enhance, calls));
        PipelineConfig config;
        config.stepEnabled[0] = config.stepEnabled[1] = true;
        std::swap(config.stepOrder[0], config.stepOrder[1]);
        PipelineContext context;
        context.config = std::make_shared<const PipelineConfig>(config);
        pipeline.run(context);
        QCOMPARE(calls, QList<int>({1, 0}));
        calls.clear();
        config.stepOrder = {0, 0, -1, 99, 1, 2, 3, 4, 5};
        config.stepEnabled[1] = false;
        context.config = std::make_shared<const PipelineConfig>(config);
        pipeline.run(context);
        QCOMPARE(calls, QList<int>({0}));
    }
    void reliableOverflowIsReported() {
        PipelineScheduler scheduler(echo);
        scheduler.setMaxQueueSize(1);
        QSignalSpy rejected(&scheduler, &PipelineScheduler::rejected);
        cv::Mat image(1, 1, CV_8UC1, cv::Scalar(1));
        QVERIFY(scheduler.submit(image, {}) >= 0);
        QCOMPARE(scheduler.submit(image, {}), qint64(-1));
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(scheduler.pendingCount(), 1);
    }
    void previewDoesNotDiscardReliableTasks() {
        PipelineScheduler scheduler(echo);
        QSignalSpy cancelled(&scheduler, &PipelineScheduler::cancelled);
        cv::Mat image(1, 1, CV_8UC1, cv::Scalar(1));
        scheduler.submit(PipelineRequest(image, {}, 0, "same-caller"));
        scheduler.submit(PipelineRequest(image, {}, 0, "same-caller"), PipelineScheduler::Policy::LatestPreview);
        scheduler.submit(PipelineRequest(image, {}, 0, "same-caller"), PipelineScheduler::Policy::LatestPreview);
        QCOMPARE(scheduler.pendingCount(), 2);
        QCOMPARE(cancelled.size(), 1);
        scheduler.cancelAll();
        QCOMPARE(cancelled.size(), 3);
    }
    void continuousPreviewStartsWithinWindow() {
        PipelineScheduler scheduler(echo);
        scheduler.setDebounceMs(20);
        QSignalSpy finished(&scheduler, &PipelineScheduler::finished);
        QTimer producer;
        cv::Mat image(1, 1, CV_8UC1, cv::Scalar(1));
        connect(&producer, &QTimer::timeout, &scheduler, [&] {
            scheduler.submit(PipelineRequest(image, {}, 0, "frames"), PipelineScheduler::Policy::LatestPreview);
        });
        producer.start(2);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.empty(), 1000);
        producer.stop();
    }
    void exceptionsAreFailures() {
        PipelineScheduler scheduler([](const cv::Mat&, const PipelineConfig&) -> PipelineContext {
            throw std::runtime_error("inference failed");
        });
        scheduler.setDebounceMs(0);
        bool failed = false;
        connect(&scheduler, &PipelineScheduler::finished, &scheduler, [&](const PipelineResult& result) {
            failed = !result.isSuccess() && result.errorMessage().contains("inference failed");
        });
        scheduler.submit(cv::Mat(1, 1, CV_8UC1, cv::Scalar(1)), {});
        QTRY_VERIFY_WITH_TIMEOUT(failed, 1000);
    }
};

QTEST_GUILESS_MAIN(PipelineRuntimeTest)
#include "pipeline_runtime_test.moc"
