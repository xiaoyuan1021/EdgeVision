#include "application/detection_workspace.h"
#include "core/pipeline_manager.h"
#include "core/roi_manager.h"
#include "core/profile_manager.h"
#include "controllers/roi_ui_controller.h"
#include "controllers/detection_ui_controller.h"
#include "controllers/config_controller.h"
#include "controllers/pipeline_result_handler.h"
#include "widgets/tab_manager.h"
#include "widgets/i_tab_interfaces.h"
#include "widgets/video_tab_widget.h"
#include "widgets/step_config_widget.h"
#include "ui/image_view.h"
#include "ui/display_mode_manager.h"
#include "core/logger.h"
#include <QTabBar>
#include <QSignalBlocker>

DetectionWorkspace::DetectionWorkspace(QTabWidget* tabs, TabManager* tabManager,
    PipelineManager* pipeline, RoiManager& rois, ImageView* view,
    RoiUiController* roiUi, DetectionUiController* detectionUi,
    ConfigController* config, ProfileManager* profiles,
    DisplayModeManager* display, PipelineResultHandler* results, QObject* parent)
    : QObject(parent), m_tabs(tabs), m_tabManager(tabManager), m_pipelineManager(pipeline),
      m_roiManager(rois), m_view(view), m_roiUiController(roiUi),
      m_detectionUiController(detectionUi), m_configController(config),
      m_profileManager(profiles), m_displayModeManager(display), m_pipelineResultHandler(results)
{
    connect(tabManager, &TabManager::tabCreated, this, &DetectionWorkspace::onTabCreated);
    detectionUi->setupConnections(roiUi,
        [this](const QString& name) { ensureTabExists(name); },
        [this] { emit refreshRequested(); });
    connect(roiUi, &RoiUiController::detectionItemSelected, this, &DetectionWorkspace::selectDetection);
    connect(roiUi, &RoiUiController::selectedRoiDeleted, tabManager, &TabManager::clearAllResults);
    connect(roiUi, &RoiUiController::tabVisibilityUpdateNeeded, this, &DetectionWorkspace::restoreTabVisibility);
    connect(detectionUi, &DetectionUiController::tabVisibilityUpdateNeeded, this, &DetectionWorkspace::restoreTabVisibility);
}

void DetectionWorkspace::onTabCreated(const QString& tabName, QWidget* widget)
{
    auto* connectable = dynamic_cast<ISignalConnectable*>(widget);
    if (connectable) {
        connectable->connectSignals(
            {m_pipelineManager, &m_roiManager, m_view, m_roiUiController,
             [this](const BarcodeConfig& config) { m_roiUiController->updateBarcodeDetectionConfig(config); }},
            [this]() {
                emit refreshRequested();
            }
        );
    } else {
        spdlog::warn("[DetectionWorkspace] Tab '{}' 未实现 ISignalConnectable 接口，跳过信号连接", tabName.toStdString());
    }

    if (auto* configurableTab = dynamic_cast<IConfigurableTab*>(widget)) {
        m_configController->registerTab(configurableTab);
    }

    if (auto* initializableTab = dynamic_cast<ITabInitializable*>(widget)) {
        TabInitContext ctx;
        ctx.profileManager = m_profileManager;
        ctx.pipelineManager = m_pipelineManager;
        initializableTab->initializeTab(ctx);
    }

    // 视频播放时采用流式结果策略，模型执行仍由 PipelineExecutor 管理。
    if (auto* videoTab = qobject_cast<VideoTabWidget*>(widget)) {
        connect(videoTab->getVideoManager(), &VideoManager::playbackStateChanged,
                this, [this](VideoManager::PlaybackState state) {
                    m_pipelineResultHandler->setVideoMode(state == VideoManager::PlaybackState::Playing);
                });
    }

    if (auto* stepWidget = qobject_cast<StepConfigWidget*>(widget)) {
        connect(stepWidget, &StepConfigWidget::tabsNeeded, this, [this](const QStringList& tabNames) {
            for (const auto& name : tabNames) {
                m_tabManager->ensureTab(name);
            }
            // 按指定顺序重排 TabBar
            auto* tabBar = m_tabs->tabBar();
            for (int desired = 0; desired < tabNames.size(); ++desired) {
                for (int cur = desired; cur < tabBar->count(); ++cur) {
                    if (tabBar->tabText(cur) == tabNames[desired]) {
                        if (cur != desired) {
                            tabBar->moveTab(cur, desired);
                        }
                        break;
                    }
                }
            }
        });
    }
}

void DetectionWorkspace::selectDetection(const QString& roiId, const QString& detectionId)
{
    m_pipelineManager->clearLastResult();
    m_tabManager->clearAllResults();
    m_detectionUiController->onDetectionItemSelected(roiId, detectionId, m_tabManager, m_pipelineManager);

    // 更新显示模式管理器的Tab状态（switchToTabConfig内部阻塞了信号，
    // 导致onTabChanged未触发，m_lastMode与实际Tab不同步）
    m_displayModeManager->onTabChanged(m_tabs->currentIndex());

    if (auto* roi = m_roiManager.getRoiConfig(roiId)) {
        if (const auto* item = roi->getDetectionItem(detectionId)) {
            m_view->setZoomEnabled(item->type != DetectionType::VideoDetection);
        }
    }
}

void DetectionWorkspace::ensureTabExists(const QString& tabName)
{
    m_tabManager->ensureTab(tabName);
}

void DetectionWorkspace::restoreTabVisibility()
{
    const QSignalBlocker blocker(m_tabs);
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->setTabVisible(i, false);
    }

    // 检测项页面优先；只有 ROI 而没有检测项时显示步骤配置。
    if (m_roiUiController->autoSelectFirstDetectionItem()) return;
    if (m_roiManager.getRoiConfigCount() == 0) return;

    ensureTabExists("步骤");
    if (auto* stepWidget = m_tabManager->getTabAs<StepConfigWidget>("步骤")) {
        stepWidget->loadFromConfig(m_pipelineManager->getConfigSnapshot());
        stepWidget->applyTabVisibility();
    }
}

void DetectionWorkspace::openPipelineConfig()
{
    spdlog::info("[DetectionWorkspace] 用户点击自定义Pipeline按钮");

    // 如果没有ROI，自动创建一个"整图"ROI（silent模式，不触发pipeline）
    QString roiId = m_roiUiController->getCurrentSelectedRoiId();
    if (roiId.isEmpty()) {
        spdlog::info("[DetectionWorkspace] 无选中ROI，自动创建整图ROI");
        roiId = m_roiUiController->addFullImageRoi(true);  // silent=true
    }

    // Opening the editor must preserve edits already in the live snapshot.
    QString currentImageId = m_roiManager.getCurrentImageId();
    if (!currentImageId.isEmpty()) {
        m_roiManager.saveImagePipelineConfig(currentImageId, m_pipelineManager->getConfigSnapshot());
        m_roiUiController->saveCurrentRoiPipelineConfig();
        // 通知所有Tab更新UI（包括StepConfigWidget）
        m_roiUiController->notifyConfigChanged(m_pipelineManager->getConfigSnapshot());
    }

    // 跳转到「步骤」Tab（Pipeline配置页面）
    ensureTabExists("步骤");

    const int index = m_tabs->indexOf(m_tabManager->getTab("步骤"));
    if (index >= 0) {
        m_tabs->setTabVisible(index, true);
        m_tabs->setCurrentIndex(index);
    }
}

void DetectionWorkspace::addDetection()
{
    QString roiId = m_roiUiController->getCurrentSelectedRoiId();
    if (roiId.isEmpty()) roiId = m_roiUiController->addFullImageRoi(true);
    if (!roiId.isEmpty()) m_detectionUiController->onAddDetectionClicked(roiId);
}

void DetectionWorkspace::deleteDetection(QTreeWidget* tree)
{
    m_detectionUiController->handleDeleteFromTree(tree);
}

void DetectionWorkspace::stopVideo()
{
    if (auto* tab = m_tabManager->getTabAs<VideoTabWidget>("视频")) {
        tab->getVideoManager()->stop();
        tab->disconnect();
        tab->getVideoManager()->disconnect();
    }
}
