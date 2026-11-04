#include <QtTest>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QStatusBar>
#include <QListWidget>
#include <QGraphicsPixmapItem>
#include <QSemaphore>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QDir>
#include <QPainter>
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
#include "widgets/line_tab_widget.h"
#include "widgets/ocr_tab_widget.h"
#include "widgets/object_detection_tab_widget.h"
#include "widgets/step_config_widget.h"
#include "controllers/image_list_manager.h"
#include "ui/image_view.h"
#include "ui/image_view_controls.h"
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
    void stepCardsReflowAndRepeatedSelectionKeepsOneLayout() {
        PipelineManager pipeline;
        StepConfigWidget page(&pipeline);
        page.resize(340, 700);
        page.show();
        auto* available = page.findChild<QWidget*>("availableSteps");
        QVERIFY(available);
        QLayout* layout = available->layout();
        const int count = layout->count();
        QVERIFY(count > 1);
        auto* card = qobject_cast<QFrame*>(layout->itemAt(0)->widget());
        auto* check = card->findChild<QCheckBox*>();
        QVERIFY(check);
        for (int i = 0; i < 12; ++i) {
            check->setChecked(true);
            QCOMPARE(layout->count(), count - 1);
            check->setChecked(false);
            QCOMPARE(layout->count(), count);
        }
        for (int width : {340, 600, 340}) {
            page.resize(width, 700);
            QTest::qWait(20);
            QCOMPARE(available->layout(), layout);
            for (int i = 0; i < layout->count(); ++i) {
                const auto a = layout->itemAt(i)->widget()->geometry();
                QVERIFY(available->rect().contains(a));
                for (int j = i + 1; j < layout->count(); ++j)
                    QVERIFY(!a.intersects(layout->itemAt(j)->widget()->geometry()));
            }
        }
        QCOMPARE(pipeline.scheduler()->pendingCount(), 0);
    }

    void viewportFitManualZoomAndRefreshHaveDistinctLifetimes() {
        ImageView view;
        view.resize(640, 480);
        view.show();
        QImage image(1000, 600, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        view.setImage(image);
        QVERIFY(view.isFitToWindow());
        const double firstFit = view.zoomFactor();
        view.resize(900, 650);
        QCoreApplication::processEvents();
        QVERIFY(view.zoomFactor() > firstFit);
        view.zoomActualSize();
        QCOMPARE(view.zoomFactor(), 1.0);
        QVERIFY(!view.isFitToWindow());
        view.horizontalScrollBar()->setValue(80);
        const int scroll = view.horizontalScrollBar()->value();
        view.setImageKeepZoom(image);
        QCOMPARE(view.zoomFactor(), 1.0);
        QCOMPARE(view.horizontalScrollBar()->value(), scroll);
        view.resize(700, 500);
        QCoreApplication::processEvents();
        QCOMPARE(view.zoomFactor(), 1.0);
        for (int i = 0; i < 100; ++i) view.zoomIn();
        QVERIFY(qAbs(view.zoomFactor() - 64.0) < 0.0001);
        for (int i = 0; i < 100; ++i) view.zoomOut();
        QVERIFY(qAbs(view.zoomFactor() - 0.01) < 0.0001);
        const double beforeWheel = view.zoomFactor();
        QWheelEvent horizontal(QPointF(50, 50), QPointF(50, 50), {}, QPoint(120, 0),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &horizontal);
        QCOMPARE(view.zoomFactor(), beforeWheel);
        view.setImageKeepZoom(image.scaled(200, 100));
        QVERIFY(view.isFitToWindow());
        QVERIFY(view.zoomFactor() > 1.0);
        view.clear();
        QVERIFY(view.imageSize().isEmpty());
        view.zoomIn();
        QCOMPARE(view.zoomFactor(), 1.0);
    }

    void viewportControlsFollowImageAndVideoState() {
        ImageView view;
        ImageViewControls controls(&view);
        auto* actual = controls.findChild<QToolButton*>("btn_actualSize");
        auto* fit = controls.findChild<QToolButton*>("btn_fitImage");
        QVERIFY(!actual->isEnabled());
        QImage image(800, 600, QImage::Format_RGB32);
        image.fill(Qt::white);
        view.setImage(image);
        QVERIFY(actual->isEnabled());
        actual->click();
        QCOMPARE(view.zoomFactor(), 1.0);
        QVERIFY(!fit->isChecked());
        QCOMPARE(controls.findChild<QLabel*>("label_imageInfo")->text(), QString("800 × 600"));
        fit->click();
        QVERIFY(view.isFitToWindow());
        QVERIFY(fit->isChecked());
        view.setZoomEnabled(false);
        QVERIFY(!actual->isEnabled());
        const double frozen = view.zoomFactor();
        view.zoomIn();
        QCOMPARE(view.zoomFactor(), frozen);
        view.setZoomEnabled(true);
        QVERIFY(actual->isEnabled());
        view.clear();
        QVERIFY(!actual->isEnabled());
    }

    void pipelineAndCachedDisplayPreserveManualZoom() {
        WorkspaceFixture f;
        f.rois.switchToImage(f.addImage("zoom"));
        connect(f.pipeline.scheduler(), &PipelineScheduler::finished,
            &f.results, &PipelineResultHandler::onPipelineResult);
        f.workspace.ensureTabExists("增强");
        f.view.zoomActualSize();
        f.session.requestRefresh();
        QTRY_VERIFY(f.pipeline.hasLastResult());
        QCOMPARE(f.view.zoomFactor(), 1.0);
        QVERIFY(!f.view.isFitToWindow());
        const int index = f.tabs.indexOf(f.tabManager.getTab("增强"));
        f.tabs.setCurrentIndex(index);
        f.display.onTabChanged(index);
        QCOMPARE(f.view.zoomFactor(), 1.0);
        QVERIFY(!f.view.isFitToWindow());
    }

    void workspaceFitsLaptopAndDesktopWithScrollableParameters() {
        MainWindow window(nullptr, MqttConfig{});
        window.setWindowState(Qt::WindowNoState);
        auto* view = window.findChild<ImageView*>();
        auto* parameters = window.findChild<QScrollArea*>("scrollArea_detection");
        auto* resources = window.findChild<QScrollArea*>("scrollArea_resources");
        QVERIFY(view && parameters && resources);
        auto* session = window.findChild<InspectionSession*>();
        auto* workspace = window.findChild<DetectionWorkspace*>();
        const auto output = qEnvironmentVariable("EDGEVISION_UI_CAPTURE_DIR");
        if (!output.isEmpty()) QVERIFY(QDir().mkpath(output));
        window.resize(1280, 720);
        window.show();
        QTest::qWait(50);
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/workspace-empty.png"));
        cv::Mat sample(900, 1200, CV_8UC3, cv::Scalar(232, 232, 232));
        cv::rectangle(sample, cv::Rect(180, 180, 840, 540), cv::Scalar(85, 95, 105), -1);
        cv::circle(sample, cv::Point(420, 450), 135, cv::Scalar(95, 190, 85), -1);
        cv::circle(sample, cv::Point(780, 450), 135, cv::Scalar(80, 150, 230), -1);
        session->loadImage(sample, {});
        workspace->openPipelineConfig();
        workspace->ensureTabExists("增强");
        for (const QSize size : {QSize(1280, 720), QSize(1920, 1080)}) {
            window.resize(size);
            QTest::qWait(50);
            QCOMPARE(window.size(), size);
            QVERIFY(view->viewport()->width() >= 320);
            QVERIFY(view->viewport()->height() >= 400);
            QVERIFY(resources->isVisible());
            QVERIFY(parameters->isVisible());
            const auto imageRect = view->mapFromScene(view->sceneRect()).boundingRect();
            QVERIFY(view->viewport()->rect().adjusted(-1, -1, 1, 1).contains(imageRect));
            if (!output.isEmpty()) QVERIFY(window.grab().save(output + QString("/workspace-%1.png").arg(size.width())));
        }
        window.resize(1280, 600);
        QTest::qWait(50);
        QCOMPARE(window.height(), 600);
        QVERIFY(resources->verticalScrollBar()->maximum() > 0);
    }

    void selectingDifferentDetectorTypesPreservesItemSettingsWithoutRunning() {
        WorkspaceFixture f;
        f.rois.switchToImage(f.addImage("parts"));
        RoiConfig roi("part", QRectF(0, 0, 20, 20));
        LineDetectConfig line;
        line.threshold = 73;
        line.enhance.brightness = 17;
        OcrDetectionConfig ocr;
        ocr.expectedText = "ABC-123";
        ocr.matchExact = true;
        ocr.enhance.brightness = -8;
        ObjectDetectionConfig object;
        object.modelPath = "not-loaded.onnx";
        object.expectedCount = 3;
        BlobAnalysisConfig blob;
        blob.minBlobCount = 2;
        blob.maxBlobCount = 9;
        for (const auto& [type, config] : std::initializer_list<std::pair<DetectionType, QJsonObject>>{
            {DetectionType::Line, line.toJson()}, {DetectionType::Ocr, ocr.toJson()},
            {DetectionType::ObjectDetection, object.toJson()}, {DetectionType::Blob, blob.toJson()}}) {
            DetectionItem item(detectionTypeToString(type), type);
            item.config = config;
            roi.detectionItems.append(item);
        }
        f.rois.addRoiConfig(roi);
        for (const auto& item : roi.detectionItems) {
            f.detectionUi.onDetectionItemSelected(roi.roiId, item.itemId, &f.tabManager, &f.pipeline);
            QCOMPARE(f.pipeline.scheduler()->pendingCount(), 0);
            QCOMPARE(f.rois.getRoiConfig(roi.roiId)->getDetectionItem(item.itemId)->config, item.config);
            QCOMPARE(f.rois.getRoiConfig(roi.roiId)->pipelineConfig.toJson(), f.pipeline.getConfigSnapshot().toJson());
            const auto current = f.pipeline.getConfigSnapshot();
            if (item.type == DetectionType::Line) {
                QCOMPARE(current.lineDetect.threshold, 73);
                QCOMPARE(current.enhance.brightness, 17);
                auto* tab = f.tabManager.getTabAs<LineDetectTabWidget>("直线");
                QVERIFY(tab);
                PipelineConfig rendered;
                tab->saveToConfig(rendered);
                QCOMPARE(rendered.lineDetect.threshold, 73);
            } else if (item.type == DetectionType::Ocr) {
                QCOMPARE(current.enhance.brightness, -8);
                auto* tab = f.tabManager.getTabAs<OcrTabWidget>("文字识别");
                QVERIFY(tab);
                QCOMPARE(tab->findChild<QLineEdit*>()->text(), QString("ABC-123"));
                QVERIFY(tab->findChild<QCheckBox*>()->isChecked());
            } else if (item.type == DetectionType::ObjectDetection) {
                auto* tab = f.tabManager.getTabAs<ObjectDetectionTabWidget>("目标检测");
                QVERIFY(tab);
                QCOMPARE(tab->getDetectionConfig().toJson(), object.toJson());
            } else {
                QCOMPARE(current.judge.minRegionCount, 2);
                QCOMPARE(current.judge.maxRegionCount, 9);
            }
        }
    }

    void batchShutdownSuppressesPendingCompletionAndRestart() {
        RoiManager rois;
        rois.setFullImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(0)));
        rois.addRoiConfig(RoiConfig("part", QRectF(0, 0, 20, 20)));
        AutoDetectionController detection(&rois);
        QSignalSpy processed(&detection, &AutoDetectionController::imageProcessed);
        detection.startDetection();
        detection.shutdown();
        QCoreApplication::processEvents();
        QVERIFY(processed.isEmpty());
        QVERIFY(!detection.isRunning());
        detection.startDetection();
        QVERIFY(!detection.isRunning());
    }

    void mainWindowClosesNormallyWithPendingWorkAndCreatedTabs() {
        auto window = std::make_unique<MainWindow>(nullptr, MqttConfig{});
        auto* session = window->findChild<InspectionSession*>();
        auto* workspace = window->findChild<DetectionWorkspace*>();
        auto* pipeline = window->findChild<PipelineManager*>();
        QVERIFY(session && workspace && pipeline);
        session->loadImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(42)), {});
        workspace->ensureTabExists("视频");
        workspace->ensureTabExists("条码");
        auto* pipelineButton = window->findChild<QPushButton*>("btn_pipelineConfig");
        QVERIFY(pipelineButton);
        pipelineButton->click();
        QVERIFY(workspace && window->findChild<TabManager*>()->isCreated("步骤"));
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
