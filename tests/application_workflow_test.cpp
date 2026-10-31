#include <QtTest>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QStatusBar>
#include <QListWidget>
#include <QGraphicsPixmapItem>
#include <QSemaphore>
#include <atomic>
#include <limits>
#include "application/inspection_session.h"
#include "application/detection_workspace.h"
#include "application/remote_inspection_controller.h"
#include "core/pipeline_manager.h"
#include "core/profile_manager.h"
#include "core/mqtt_manager.h"
#include "controllers/roi_ui_controller.h"
#include "controllers/detection_ui_controller.h"
#include "controllers/config_controller.h"
#include "controllers/pipeline_result_handler.h"
#include "controllers/auto_detection_controller.h"
#include "widgets/tab_manager.h"
#include "controllers/image_list_manager.h"
#include "ui/image_view.h"
#include "ui/display_mode_manager.h"
#include "ui/file_manager.h"
#include "ui/mainwindow.h"
#include "algorithm/image_utils.h"

// Uses the production composition without opening a broker, camera or window.
struct WorkspaceFixture {
    RoiManager rois;
    PipelineManager pipeline;
    QTabWidget tabs;
    ImageView view;
    QStatusBar status;
    QTreeWidget tree;
    TabManager tabManager{&tabs, &pipeline, &rois, &view, &status};
    RoiUiController roiUi{rois, &pipeline, &view, &status};
    DetectionUiController detectionUi{rois, &tabs};
    ConfigController config{&pipeline, rois};
    ProfileManager profiles{&rois, &pipeline};
    DisplayModeManager display{&tabs, &pipeline, &view};
    PipelineResultHandler results;
    InspectionSession session{&pipeline, &rois, &roiUi, &tabManager, &view, &display};
    DetectionWorkspace workspace{&tabs, &tabManager, &pipeline, rois, &view,
        &roiUi, &detectionUi, &config, &profiles, &display, &results};
    WorkspaceFixture() {
        roiUi.setupTreeView(&tree);
        roiUi.setupDisplayConnections(&tabManager);
        results.setDependencies(&tabManager, &rois, &view, &pipeline);
        QObject::connect(&session, &InspectionSession::imageSessionChanged,
            &workspace, &DetectionWorkspace::restoreTabVisibility);
        QObject::connect(&workspace, &DetectionWorkspace::refreshRequested,
            &session, &InspectionSession::requestRefresh);
    }
    ~WorkspaceFixture() {
        workspace.stopVideo();
        session.shutdown();
        while (tabs.count()) delete tabs.widget(0);
    }
    QString addImage(const QString& name) {
        return rois.addImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(42)), name);
    }
};

class ApplicationWorkflowTest : public QObject {
    Q_OBJECT
private slots:
    void mainWindowClosesNormallyWithPendingWorkAndCreatedTabs() {
        auto window = std::make_unique<MainWindow>(nullptr, MqttConfig{});
        auto* session = window->findChild<InspectionSession*>();
        auto* workspace = window->findChild<DetectionWorkspace*>();
        auto* pipeline = window->findChild<PipelineManager*>();
        QVERIFY(session && workspace && pipeline);
        session->loadImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(42)), {});
        workspace->ensureTabExists("视频");
        workspace->ensureTabExists("条码");
        workspace->openPipelineConfig();
        session->requestRefresh();
        QVERIFY(pipeline->scheduler()->pendingCount() > 0);
        QPointer<MainWindow> lifetime(window.get());
        window.reset();
        QVERIFY(lifetime.isNull());
        QCoreApplication::processEvents();
    }
    void roiIntersectionDoesNotEnlargeNegativeOrInvalidRegions() {
        QCOMPARE(ImageUtils::mapRoiToCvRect(QRectF(-5, -3, 10, 10), 20, 20), cv::Rect(0, 0, 5, 7));
        QCOMPARE(ImageUtils::mapRoiToCvRect(QRectF(2.5, 2.5, 2, 2), 20, 20), cv::Rect(2, 2, 3, 3));
        QVERIFY(ImageUtils::mapRoiToCvRect(QRectF(25, 0, 10, 10), 20, 20).empty());
        QVERIFY(ImageUtils::mapRoiToCvRect(QRectF(std::numeric_limits<double>::quiet_NaN(), 0, 10, 10), 20, 20).empty());
    }

    void invalidBatchRoiCannotSilentlyInspectTheFullImage() {
        RoiManager rois;
        rois.setFullImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(0)));
        RoiConfig outside("outside", QRectF(100, 100, 10, 10));
        rois.addRoiConfig(outside);
        rois.setActiveRoi(outside.roiId);
        QVERIFY(rois.getCurrentImage().empty());
        AutoDetectionController detection(&rois);
        detection.startDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.stats().failCount, 1);
        QVERIFY(detection.results().first().failReason.contains("ROI"));
    }
    void removingCurrentImageRestoresRemainingDetectionTabsOnce() {
        WorkspaceFixture f;
        const auto a = f.addImage("a");
        const auto b = f.addImage("b");
        f.rois.switchToImage(b);
        RoiConfig roi("part", QRectF(0, 0, 20, 20));
        roi.detectionItems = {DetectionItem("code", DetectionType::Barcode)};
        f.rois.addRoiConfig(roi);
        f.rois.switchToImage(a);
        QSignalSpy switched(&f.rois, &RoiManager::currentImageChanged);
        QVERIFY(f.rois.removeImage(a));
        QCOMPARE(switched.size(), 1);
        QCOMPARE(f.rois.getCurrentImageId(), b);
        int index = f.tabs.indexOf(f.tabManager.getTab("条码"));
        QVERIFY(index >= 0);
        QVERIFY(f.tabs.isTabVisible(index));
        QVERIFY(f.rois.switchToImage(b));
        QCOMPARE(switched.size(), 1);
        QCOMPARE(f.pipeline.scheduler()->pendingCount(), 0);
    }

    void deletingLastImageAndClearResetTheWholeSession() {
        WorkspaceFixture f;
        FileManager files;
        QListWidget list;
        QPushButton add, remove;
        ImageListManager images(f.rois, &files, &list);
        images.init(&list, &add, &remove);
        const auto id = f.addImage("only");
        f.rois.switchToImage(id);
        f.workspace.ensureTabExists("条码");
        f.session.requestRefresh();
        QVERIFY(f.rois.removeImage(id));
        QVERIFY(f.rois.getCurrentImageId().isEmpty());
        QCOMPARE(list.count(), 0);
        QCOMPARE(f.pipeline.scheduler()->pendingCount(), 0);
        QVERIFY(!f.pipeline.hasLastResult());
        for (int i = 0; i < f.tabs.count(); ++i) QVERIFY(!f.tabs.isTabVisible(i));
        for (auto* item : f.view.scene()->items())
            if (auto* pixmap = dynamic_cast<QGraphicsPixmapItem*>(item)) QVERIFY(pixmap->pixmap().isNull());
        const auto next = f.addImage("next");
        f.rois.switchToImage(next);
        f.addImage("other");
        f.rois.clear();
        QCOMPARE(list.count(), 0);
        QVERIFY(f.tree.topLevelItemCount() == 0);
        QVERIFY(f.addImage("fresh") != next);
    }

    void switchingImagesRestoresConfigWithoutExecuting() {
        WorkspaceFixture f;
        const auto a = f.addImage("a");
        const auto b = f.addImage("b");
        f.rois.switchToImage(a);
        f.pipeline.updateConfig([](PipelineConfig& c) { c.enhance.brightness = 37; });
        f.rois.switchToImage(b);
        f.pipeline.updateConfig([](PipelineConfig& c) { c.enhance.brightness = -12; });
        f.rois.switchToImage(a);
        QCOMPARE(f.pipeline.getConfigSnapshot().enhance.brightness, 37);
        f.workspace.openPipelineConfig();
        QCOMPARE(f.pipeline.getConfigSnapshot().enhance.brightness, 37);
        f.session.loadImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(99)), "replacement.png");
        QCOMPARE(f.pipeline.getConfigSnapshot().enhance.brightness, 37);
        QCOMPARE(f.rois.getFullImage().at<cv::Vec3b>(0, 0)[0], uchar(99));
        QCOMPARE(f.pipeline.scheduler()->pendingCount(), 0);
    }

    void batchOwnsImagesEvenIfSourcesAreReplacedOrRemoved() {
        RoiManager rois;
        for (int i = 0; i < 2; ++i) {
            const auto id = rois.addImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(42)),
                QString::number(i), "does-not-exist.png");
            rois.switchToImage(id);
            rois.addRoiConfig(RoiConfig("part", QRectF(0, 0, 20, 20)));
        }
        AutoDetectionController detection(&rois);
        detection.startDetection();
        detection.pauseDetection();
        QTRY_COMPARE_WITH_TIMEOUT(detection.stats().processedCount, 1, 10000);
        rois.clear();
        detection.resumeDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!detection.isRunning(), 10000);
        QCOMPARE(detection.stats().processedCount, 2);
        QCOMPARE(detection.stats().failCount, 0);
    }

    void remoteCaptureIsSingleFlightAndRejectsLateFrames() {
        WorkspaceFixture f;
        const auto a = f.addImage("a");
        const auto b = f.addImage("b");
        f.rois.switchToImage(a);
        MqttManager mqtt;
        AutoDetectionController detection(&f.rois);
        auto gate = std::make_shared<QSemaphore>();
        std::atomic<int> captures{0};
        RemoteInspectionController remote(&mqtt, &detection, &f.rois, &f.pipeline,
            &f.tabManager, nullptr, [gate, &captures] {
                ++captures;
                gate->tryAcquire(1, 2000);
                return cv::Mat(20, 20, CV_8UC3, cv::Scalar(99));
            });
        QSignalSpy refreshed(&remote, &RemoteInspectionController::refreshRequested);
        QSignalSpy messages(&remote, &RemoteInspectionController::statusMessage);
        remote.capture();
        remote.capture();
        QTRY_COMPARE_WITH_TIMEOUT(captures.load(), 1, 1000);
        f.rois.switchToImage(b);
        gate->release();
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 2, 3000);
        QCOMPARE(refreshed.size(), 0);
        QCOMPARE(f.rois.getFullImage().at<cv::Vec3b>(0, 0)[0], uchar(42));
        remote.capture();
        gate->release();
        QTRY_COMPARE_WITH_TIMEOUT(refreshed.size(), 1, 3000);
        QCOMPARE(f.rois.getFullImage().at<cv::Vec3b>(0, 0)[0], uchar(99));
        remote.shutdown();
        remote.capture();
        QCOMPARE(captures.load(), 2);
    }

    void shutdownJoinsWorkAndRejectsFurtherRequests() {
        std::atomic<bool> finished{false};
        PipelineScheduler scheduler([&finished](const cv::Mat&, const PipelineConfig&) {
            QThread::msleep(200);
            finished = true;
            return PipelineContext{};
        });
        scheduler.setDebounceMs(0);
        QSignalSpy delivered(&scheduler, &PipelineScheduler::finished);
        scheduler.submit(cv::Mat(5, 5, CV_8UC3), {});
        QTRY_VERIFY(scheduler.isProcessing());
        scheduler.shutdown();
        QVERIFY(finished.load());
        QVERIFY(!scheduler.isProcessing());
        QCOMPARE(scheduler.submit(cv::Mat(5, 5, CV_8UC3), {}), qint64(-1));
        QCoreApplication::processEvents();
        QCOMPARE(delivered.size(), 0);
        scheduler.shutdown();
    }
};

QTEST_MAIN(ApplicationWorkflowTest)
#include "application_workflow_test.moc"
