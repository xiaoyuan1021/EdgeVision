#include <QtTest>
#include <QSignalSpy>
#include "controllers/auto_detection_controller.h"

namespace {
struct Sample {
    QString imageId;
    QString roiId;
};

Sample addSample(RoiManager& rois, const QString& name, bool outside)
{
    const auto id = rois.addImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(0)), name);
    rois.switchToImage(id);
    RoiConfig roi("part", outside ? QRectF(40, 40, 10, 10) : QRectF(0, 0, 20, 20));
    DetectionItem item("count", DetectionType::Blob);
    PipelineConfig config;
    config.stepEnabled.fill(false);
    config.judge.minRegionCount = 0;
    config.judge.maxRegionCount = 0;
    item.savePipeline(config);
    roi.detectionItems.append(item);
    rois.addRoiConfig(roi);
    return {id, roi.roiId};
}

const InspectionRecord* findRecord(const QList<InspectionRecord>& records, const QString& imageId, int round)
{
    for (const auto& record : records)
        if (record.result.imageId == imageId && record.round == round) return &record;
    return nullptr;
}
}

class InspectionRetryTest : public QObject
{
    Q_OBJECT
private slots:
    void retryUsesIdentityAndCurrentEditsWhilePreservingHistory() {
        RoiManager rois;
        const auto failing = addSample(rois, "same-name", true);
        const auto passing = addSample(rois, "same-name", false);
        QVERIFY(failing.imageId != passing.imageId);
        QVERIFY(rois.getImageFilePath(failing.imageId).isEmpty());
        QVERIFY(rois.getImageFilePath(passing.imageId).isEmpty());
        AutoDetectionController detection(&rois);
        detection.startDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.stats().passCount, 1);
        QCOMPARE(detection.stats().failCount, 1);
        QCOMPARE(detection.records().size(), 2);
        QCOMPARE(detection.failedImageIds(), QStringList{failing.imageId});
        const auto* original = findRecord(detection.records(), failing.imageId, 1);
        QVERIFY(original);
        QCOMPARE(original->status, InspectionStatus::Failed);
        QCOMPARE(original->result.roiResults.size(), 1);
        const auto originalDetails = original->result.toJson();

        QSignalSpy snapshot(&detection, &AutoDetectionController::snapshotRequested);
        connect(&detection, &AutoDetectionController::snapshotRequested, &detection, [&] {
            rois.switchToImage(failing.imageId);
            rois.getRoiConfig(failing.roiId)->roiRect = QRectF(0, 0, 20, 20);
        });
        QSignalSpy processed(&detection, &AutoDetectionController::imageProcessed);
        QVERIFY(detection.retryFailed());
        QVERIFY(detection.isRunning());
        QVERIFY(!detection.retryFailed());
        QCOMPARE(snapshot.size(), 1);
        QCOMPARE(detection.records().size(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);

        QCOMPARE(detection.stats().totalCount, 1);
        QCOMPARE(detection.stats().processedCount, 1);
        QCOMPARE(detection.stats().passCount, 1);
        QCOMPARE(detection.stats().failCount, 0);
        QCOMPARE(processed.size(), 1);
        QCOMPARE(processed.first().at(0).toInt(), 0);
        QVERIFY(processed.first().at(2).toBool());
        QCOMPARE(detection.results().size(), 1);
        QCOMPARE(detection.results().at(processed.first().at(0).toInt()).imageId, failing.imageId);
        const auto* retry = findRecord(detection.records(), failing.imageId, 2);
        QVERIFY(retry);
        QCOMPARE(retry->status, InspectionStatus::Passed);
        QCOMPARE(retry->result.roiResults.size(), 1);
        QCOMPARE(retry->result.roiResults.first().roiId, failing.roiId);
        QVERIFY(!findRecord(detection.records(), passing.imageId, 2));
        const auto* retained = findRecord(detection.records(), failing.imageId, 1);
        QVERIFY(retained);
        QCOMPARE(retained->result.toJson(), originalDetails);
        QVERIFY(detection.failedImageIds().isEmpty());
        QVERIFY(!detection.retryFailed());
        QCOMPARE(detection.records().size(), 3);

        detection.startDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.records().size(), 2);
        QCOMPARE(detection.results().size(), 2);
        QCOMPARE(detection.stats().totalCount, 2);
        for (const auto& record : detection.records()) {
            QCOMPARE(record.round, 1);
            QCOMPARE(record.status, InspectionStatus::Passed);
        }
    }

    void pendingRetryDoesNotHideThePreviousCompletedFailure() {
        RoiManager rois;
        const auto a = addSample(rois, "a", true);
        const auto b = addSample(rois, "b", true);
        AutoDetectionController detection(&rois);
        detection.startDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.failedImageIds().size(), 2);

        const auto stop = connect(&detection, &AutoDetectionController::imageProcessed,
            &detection, &AutoDetectionController::stopDetection);
        QVERIFY(detection.retryFailed());
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        disconnect(stop);
        QCOMPARE(detection.stats().processedCount, 1);
        QCOMPARE(detection.results().size(), 1);
        QCOMPARE(detection.records().size(), 4);
        const auto pending = detection.records().last();
        QCOMPARE(pending.round, 2);
        QCOMPARE(pending.status, InspectionStatus::Pending);
        QCOMPARE(pending.completedAtMs, 0);
        QVERIFY(detection.failedImageIds().contains(pending.result.imageId));
        QCOMPARE(detection.failedImageIds().size(), 2);

        for (const auto& sample : {a, b}) {
            QVERIFY(rois.switchToImage(sample.imageId));
            rois.getRoiConfig(sample.roiId)->roiRect = QRectF(0, 0, 20, 20);
        }
        QVERIFY(detection.retryFailed());
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.records().size(), 6);
        QCOMPARE(detection.stats().processedCount, 2);
        QCOMPARE(detection.stats().passCount, 2);
        const auto* oldPending = findRecord(detection.records(), pending.result.imageId, 2);
        const auto* latestA = findRecord(detection.records(), a.imageId, 3);
        const auto* latestB = findRecord(detection.records(), b.imageId, 3);
        QVERIFY(oldPending && latestA && latestB);
        QCOMPARE(oldPending->status, InspectionStatus::Pending);
        QCOMPARE(latestA->status, InspectionStatus::Passed);
        QCOMPARE(latestB->status, InspectionStatus::Passed);
        QVERIFY(detection.failedImageIds().isEmpty());
    }

    void unavailableInspectionCannotTurnPreviousFailureIntoPass_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("image-removed") << 0;
        QTest::newRow("roi-removed") << 1;
        QTest::newRow("all-rois-disabled") << 2;
        QTest::newRow("all-items-disabled") << 3;
    }

    void unavailableInspectionCannotTurnPreviousFailureIntoPass() {
        QFETCH(int, change);
        RoiManager rois;
        const auto sample = addSample(rois, "sample", true);
        AutoDetectionController detection(&rois);
        detection.startDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.failedImageIds(), QStringList{sample.imageId});
        const auto originalDetails = detection.records().first().result.toJson();

        QVERIFY(rois.switchToImage(sample.imageId));
        if (change == 0) {
            QVERIFY(rois.removeImage(sample.imageId));
        } else if (change == 1) {
            QVERIFY(rois.removeRoiConfig(sample.roiId));
        } else {
            auto* roi = rois.getRoiConfig(sample.roiId);
            QVERIFY(roi);
            roi->roiRect = QRectF(0, 0, 20, 20);
            if (change == 2) roi->isActive = false;
            else for (auto& item : roi->detectionItems) item.enabled = false;
        }
        if (detection.retryFailed())
            QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QVERIFY(!detection.isRunning());
        QCOMPARE(detection.stats().passCount, 0);
        for (const auto& record : detection.records())
            QVERIFY(record.status != InspectionStatus::Passed);
        QCOMPARE(detection.records().first().result.toJson(), originalDetails);
        QVERIFY(detection.failedImageIds().contains(sample.imageId));
    }
};

QTEST_MAIN(InspectionRetryTest)
#include "inspection_retry_test.moc"
