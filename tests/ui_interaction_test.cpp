#include <QtTest>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include "pipeline_access_stub.h"
#include "widgets/image_tab_widget.h"
#include "widgets/enhance_tab_widget.h"
#include "widgets/filter_tab_widget.h"
#include "widgets/extract_tab_widget.h"
#include "widgets/line_tab_widget.h"
#include "widgets/judge_tab_widget.h"
#include "widgets/image_filter_tab_widget.h"
#include "widgets/barcode_tab_widget.h"
#include "controllers/roi_detection_config_controller.h"
#include "controllers/roi_ui_controller.h"
#include "controllers/image_list_manager.h"
#include "ui/file_manager.h"
#include "controllers/detection_ui_controller.h"
#include "controllers/auto_detection_controller.h"
#include "controllers/pipeline_result_handler.h"
#include "widgets/tab_manager.h"
#include "ui/image_view.h"
#include "ui/log_page.h"
#include "core/pipeline_manager.h"
#include "core/roi_manager.h"
#include <QTabWidget>
#include <QStatusBar>

template<class T> T* control(QWidget& widget, const char* name) {
    auto* found = widget.findChild<T*>(QString::fromLatin1(name));
    if (!found) qFatal("Missing UI control: %s", name);
    return found;
}

class UiInteractionTest : public QObject {
    Q_OBJECT
private slots:
    void imageApplyCommitsAndExecutesOnce() {
        PipelineStub pipeline;
        ImageTabWidget widget(&pipeline);
        int executions = 0;
        const auto run = [&] { ++executions; };
        widget.connectSignals({}, run);
        widget.connectSignals({}, run); // Reinitialization cannot multiply bindings.
        auto config = pipeline.value;
        config.colorFilter.channel = ChannelMode::R;
        widget.loadFromConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QCOMPARE(executions, 0);
        control<QPushButton>(widget, "btn_applyChannel")->click();
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(executions, 1);
        QCOMPARE(pipeline.value.colorFilter.channel, ChannelMode::R);
        for (int i = 0; i < widget.metaObject()->methodCount(); ++i)
            QVERIFY(!widget.metaObject()->method(i).name().startsWith("on_"));
    }

    void enhancementRenderResetAndUndoStayWithinTheirScope() {
        PipelineStub pipeline;
        pipeline.value.objectDetection.modelPath = "keep.onnx";
        pipeline.value.barcode.timeout = 1234;
        EnhanceTabWidget widget(&pipeline);
        QSignalSpy execute(&widget, &EnhanceTabWidget::processRequested);
        auto config = pipeline.value;
        config.enhance.brightness = 35;
        widget.loadFromConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QVERIFY(execute.isEmpty());
        control<QPushButton>(widget, "btn_resetBC")->click();
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(execute.size(), 1);
        QCOMPARE(pipeline.value.objectDetection.modelPath, QString("keep.onnx"));
        QCOMPARE(pipeline.value.barcode.timeout, 1234);
        auto* undo = control<QPushButton>(widget, "btn_undoBC");
        QVERIFY(undo->isEnabled());
        undo->click();
        QCOMPARE(pipeline.value.enhance.brightness, 35);
        config.enhance.brightness = -20;
        widget.loadFromConfig(config); // New ROI has its own undo baseline.
        QVERIFY(!undo->isEnabled());
        control<QSpinBox>(widget, "spinBox_brightness")->setValue(-10);
        QVERIFY(undo->isEnabled());
        undo->click();
        QCOMPARE(pipeline.value.enhance.brightness, -20);
    }

    void filterRendersAllChannelsWithoutEditing() {
        PipelineStub pipeline;
        FilterTabWidget widget(&pipeline);
        QSignalSpy edited(&widget, &FilterTabWidget::filterConfigChanged);
        auto config = pipeline.value;
        config.colorFilter.mode = ImageFilterMode::HSV;
        config.colorFilter.rLow = 21;
        config.colorFilter.rHigh = 201;
        config.colorFilter.hLow = 17;
        config.colorFilter.hHigh = 151;
        config.colorFilter.sLow = 32;
        widget.loadFromConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QVERIFY(edited.isEmpty());
        PipelineConfig saved = config;
        widget.saveToConfig(saved);
        QCOMPARE(saved.toJson(), config.toJson());
        control<QSpinBox>(widget, "spinBox_hsv_H_Low")->setValue(23);
        QCOMPARE(edited.size(), 1);
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(pipeline.value.colorFilter.hLow, 23);
        QCOMPARE(pipeline.value.colorFilter.rHigh, 201);
    }

    void extractionRenderIsNotAnEdit() {
        PipelineStub pipeline;
        ImageView view;
        RoiManager rois;
        QStatusBar status;
        ExtractTabWidget widget(&pipeline, &view, &rois, &status);
        widget.initialize();
        QSignalSpy edited(&widget, &ExtractTabWidget::extractionChanged);
        auto config = pipeline.value;
        config.shapeFilter.mode = FilterMode::Or;
        widget.loadFromConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QVERIFY(edited.isEmpty());
    }

    void lineSpinboxAndSliderHaveOneExecutionPath() {
        PipelineStub pipeline;
        pipeline.value.lineDetect.enabled = true;
        pipeline.value.lineDetect.rho = 2.5;
        LineDetectTabWidget widget(&pipeline);
        ImageView view;
        widget.initialize();
        widget.initialize();
        int executions = 0;
        widget.connectSignals({nullptr, nullptr, &view, nullptr, {}}, [&] { ++executions; });
        auto config = pipeline.value;
        config.lineDetect.algorithm = 1;
        config.lineDetect.threshold = 70;
        widget.loadFromConfig(config);
        QCOMPARE(pipeline.writes, 0);
        QCOMPARE(executions, 0);
        control<QSpinBox>(widget, "SpinBox_HoughPThreshold")->setValue(71);
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(executions, 1);
        QCOMPARE(control<QSlider>(widget, "Slider_HoughPThreshold")->value(), 71);
        QCOMPARE(pipeline.value.lineDetect.rho, 2.5);
        const int before = executions;
        widget.setReferenceLine(cv::Point2f(1, 1), cv::Point2f(9, 9));
        QCOMPARE(executions, before + 1);
        QVERIFY(pipeline.value.lineDetect.enableReferenceLineMatch);
        control<QPushButton>(widget, "btn_clearReferenceLine")->click();
        QCOMPARE(executions, before + 2);
        QVERIFY(!pipeline.value.lineDetect.referenceLineValid);
    }

    void denoiseApplyExecutesOnce() {
        PipelineStub pipeline;
        ImageFilterTabWidget widget(&pipeline);
        int executions = 0;
        widget.connectSignals({}, [&] { ++executions; });
        const auto buttons = widget.findChildren<QPushButton*>();
        auto found = std::find_if(buttons.begin(), buttons.end(), [](QPushButton* b) { return b->text() == "应用"; });
        QVERIFY(found != buttons.end());
        (*found)->click();
        QCOMPARE(pipeline.writes, 1);
        QCOMPARE(executions, 1);
    }

    void judgeDisplaysDeliveredResult() {
        PipelineStub pipeline;
        JudgeTabWidget widget(&pipeline);
        PipelineContext result;
        result.regionCount = 12;
        widget.updateFromPipeline(result);
        QCOMPARE(control<QLineEdit>(widget, "lineEdit_nowRegions")->text(), QString("12"));
    }

    void editingSecondItemDoesNotOverwriteFirst() {
        RoiManager rois;
        rois.setFullImage(cv::Mat(10, 10, CV_8UC3, cv::Scalar(0)));
        RoiConfig roi("part", QRectF(0, 0, 10, 10));
        roi.detectionItems = {DetectionItem("first", DetectionType::Barcode), DetectionItem("second", DetectionType::Barcode)};
        rois.addRoiConfig(roi);
        RoiDetectionConfigController controller(rois);
        controller.setSelection(roi.roiId, roi.detectionItems[1].itemId);
        BarcodeConfig config;
        config.timeout = 8765;
        controller.updateBarcodeDetectionConfig(config);
        const auto* saved = rois.getRoiConfig(roi.roiId);
        QCOMPARE(saved->detectionItems[0].config, roi.detectionItems[0].config);
        QCOMPARE(saved->detectionItems[1].config, config.toJson());
        controller.setSelection(roi.roiId, "deleted-item");
        config.timeout = 123;
        controller.updateBarcodeDetectionConfig(config);
        QCOMPARE(saved->detectionItems[1].config["timeout"].toInt(), 8765);
    }

    void detectionSelectionRestoresCorrectTabsAndPreservesCustomSteps() {
        PipelineManager pipeline;
        RoiManager rois;
        ImageView view;
        QStatusBar status;
        QTabWidget tabs;
        TabManager manager(&tabs, &pipeline, &rois, &view, &status);
        DetectionUiController controller(rois, &tabs);
        connect(&controller, &DetectionUiController::ensureTabNeeded, &manager, &TabManager::ensureTab);
        rois.setFullImage(cv::Mat(10, 10, CV_8UC3, cv::Scalar(0)));
        RoiConfig roi("part", QRectF(0, 0, 10, 10));
        DetectionItem barcode("barcode", DetectionType::Barcode);
        BarcodeConfig barcodeConfig;
        barcodeConfig.maxNumSymbols = 7;
        barcodeConfig.timeout = 4321;
        barcode.config = barcodeConfig.toJson();
        DetectionItem custom("custom", DetectionType::Custom);
        PipelineConfig customConfig;
        customConfig.stepEnabled.fill(false);
        customConfig.stepEnabled[7] = true;
        custom.savePipeline(customConfig);
        roi.detectionItems = {barcode, custom};
        rois.addRoiConfig(roi);
        controller.onDetectionItemSelected(roi.roiId, barcode.itemId, &manager, &pipeline);
        auto* barcodeTab = manager.getTabAs<BarcodeTabWidget>("条码");
        QVERIFY(barcodeTab);
        QCOMPARE(barcodeTab->getBarcodeConfig().toJson(), barcodeConfig.toJson());
        pipeline.updateConfig([](PipelineConfig& cfg) { cfg.stepEnabled[2] = true; });
        controller.onDetectionItemSelected(roi.roiId, custom.itemId, &manager, &pipeline);
        QCOMPARE(pipeline.getConfigSnapshot().stepEnabled, customConfig.stepEnabled);
        controller.onDetectionItemSelected(roi.roiId, barcode.itemId, &manager, &pipeline);
        QStringList visible;
        for (int i = 0; i < tabs.count(); ++i)
            if (tabs.isTabVisible(i)) visible.append(tabs.tabText(i));
        QCOMPARE(visible, TabConfig::getBarcodeConfig().tabNames);
    }

    void logHomeButtonHasExplicitConnection() {
        LogPage page;
        QSignalSpy home(&page, &LogPage::requestGoHome);
        control<QPushButton>(page, "btn_home")->click();
        QCOMPARE(home.size(), 1);
    }

    void imageListRenderingDoesNotSwitchBackToTheOldImage() {
        RoiManager rois;
        FileManager files;
        QWidget parent;
        QListWidget list;
        QPushButton add, remove;
        ImageListManager manager(rois, &files, &parent);
        manager.init(&list, &add, &remove);
        const auto a = rois.addImage(cv::Mat(5, 5, CV_8UC3), "a");
        const auto b = rois.addImage(cv::Mat(5, 5, CV_8UC3), "b");
        rois.switchToImage(a);
        QSignalSpy switched(&rois, &RoiManager::currentImageChanged);
        rois.switchToImage(b);
        QCOMPARE(switched.size(), 1);
        QCOMPARE(rois.getCurrentImageId(), b);
        QCOMPARE(list.currentItem()->data(Qt::UserRole).toString(), b);
        manager.refresh();
        QCOMPARE(switched.size(), 1);
        QCOMPARE(list.currentItem()->data(Qt::UserRole).toString(), b);
        list.setCurrentRow(0);
        QCOMPARE(switched.size(), 2);
    }

    void roiTreeSelectionWorksWithKeyboardAndSurvivesRefresh() {
        PipelineManager pipeline;
        RoiManager rois;
        ImageView view;
        QStatusBar status;
        QTreeWidget tree;
        RoiDetectionConfigController config(rois);
        RoiUiController controller(rois, &pipeline, &view, &status);
        controller.setDetectionConfigController(&config);
        rois.setFullImage(cv::Mat(20, 20, CV_8UC3, cv::Scalar(0)));
        RoiConfig roi("part", QRectF(0, 0, 10, 10));
        roi.detectionItems = {DetectionItem("a", DetectionType::Barcode), DetectionItem("b", DetectionType::Barcode)};
        rois.addRoiConfig(roi);
        controller.setupTreeView(&tree);
        QSignalSpy selected(&controller, &RoiUiController::detectionItemSelected);
        auto* second = tree.topLevelItem(0)->child(1);
        tree.setCurrentItem(second); // Same selection signal as keyboard navigation.
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected[0][1].toString(), roi.detectionItems[1].itemId);
        controller.refreshRoiTreeView();
        QCOMPARE(tree.currentItem()->data(0, Qt::UserRole).toString(), roi.detectionItems[1].itemId);
        QCOMPARE(selected.size(), 1);
    }

    void pauseResumeDoesNotSubmitTheSameImageTwice() {
        RoiManager rois;
        for (int i = 0; i < 2; ++i) {
            const auto id = rois.addImage(cv::Mat(10, 10, CV_8UC3, cv::Scalar(i)), QString::number(i));
            rois.switchToImage(id);
            rois.addRoiConfig(RoiConfig("part", QRectF(0, 0, 10, 10)));
        }
        AutoDetectionController controller(&rois);
        QSignalSpy images(&controller, &AutoDetectionController::imageProcessed);
        controller.startDetection();
        controller.pauseDetection();
        controller.resumeDetection();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 10000);
        QCOMPARE(images.size(), 2);
        QCOMPARE(controller.stats().processedCount, 2);
        controller.startDetection();
        controller.pauseDetection();
        QTRY_COMPARE_WITH_TIMEOUT(controller.stats().processedCount, 1, 10000);
        QVERIFY(controller.isPaused());
        controller.stopDetection();
        QVERIFY(!controller.isRunning());
        QCOMPARE(controller.stats().processedCount, 1);
    }

    void lateResultsCannotReplaceAnotherRoiOrConfiguration() {
        PipelineManager pipeline;
        RoiManager rois;
        rois.setFullImage(cv::Mat(10, 10, CV_8UC3, cv::Scalar(7)));
        RoiConfig first("first", QRectF(0, 0, 5, 5));
        RoiConfig second("second", QRectF(5, 5, 5, 5));
        rois.addRoiConfig(first);
        rois.addRoiConfig(second);
        rois.setActiveRoi(first.roiId);
        connect(&rois, &RoiManager::inputChanged, &pipeline, &PipelineManager::clearLastResult);
        PipelineResultHandler handler;
        handler.setDependencies(nullptr, &rois, nullptr, &pipeline);
        QSignalSpy accepted(&handler, &PipelineResultHandler::pipelineTimingUpdated);
        auto makeResult = [&] {
            PipelineRequest request(rois.getCurrentImage(), pipeline.getConfigSnapshot(), 0, "test",
                {rois.getCurrentImageId(), rois.getActiveRoiId(), pipeline.displayGeneration()});
            pipeline.expectRequest(request.id());
            PipelineContext ctx;
            ctx.srcBgr = request.image().clone();
            ctx.config = std::make_shared<const PipelineConfig>(request.config());
            return PipelineResult::success(std::move(request), std::move(ctx), 1);
        };
        const auto late = makeResult();
        rois.setActiveRoi(second.roiId);
        handler.onPipelineResult(late);
        QCOMPARE(accepted.size(), 0);
        QVERIFY(!pipeline.hasLastResult());
        const auto current = makeResult();
        handler.onPipelineResult(current);
        QCOMPARE(accepted.size(), 1);
        QVERIFY(pipeline.hasCachedResult(second.roiId));
        const auto oldConfig = makeResult();
        pipeline.updateConfig([](PipelineConfig& config) { ++config.enhance.brightness; });
        handler.onPipelineResult(oldConfig);
        QCOMPARE(accepted.size(), 1);
        handler.setVideoMode(true);
        handler.onPipelineResult(current);
        QCOMPARE(accepted.size(), 1);
    }
};

QTEST_MAIN(UiInteractionTest)
#include "ui_interaction_test.moc"
