#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "application/inspection_session.h"
#include "application/detection_workspace.h"
#include "application/remote_inspection_controller.h"
#include "core/logger.h"
#include "core/pipeline_manager.h"
#include "core/profile_manager.h"
#include "core/mqtt_manager.h"
#include "config/constants.h"
#include "ui/image_view.h"
#include "ui/image_view_controls.h"
#include <QTabBar>
#include "ui/system_monitor.h"
#include "ui/file_manager.h"
#include "ui/log_page.h"
#include "ui/toast_notification.h"
#include "ui/cloud_dashboard_manager.h"
#include "ui/display_mode_manager.h"
#include "controllers/image_list_manager.h"
#include "widgets/tab_manager.h"
#include "controllers/roi_ui_controller.h"
#include "controllers/roi_detection_config_controller.h"
#include "controllers/detection_ui_controller.h"
#include "controllers/config_controller.h"
#include "controllers/auto_detection_controller.h"
#include "controllers/pipeline_result_handler.h"
#include "controllers/profile_controller.h"
#include <QFile>
#include <QFileDialog>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

MainWindow::MainWindow(QWidget *parent, std::optional<MqttConfig> mqttConfig)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pipelineManager(new PipelineManager(this))
    , m_systemMonitor(new SystemMonitor(this))
    , m_fileManager(new FileManager(this))
{
    ui->setupUi(this);
    setWindowState(Qt::WindowMaximized);

    setupBasicInfrastructure();
    setupManagers();
    setupControllers();
    setupResultHandler();
    m_session = new InspectionSession(m_pipelineManager, &m_roiManager, m_roiUiController,
        m_tabManager, m_view, m_displayModeManager, this);
    m_workspace = new DetectionWorkspace(ui->tabWidget, m_tabManager, m_pipelineManager,
        m_roiManager, m_view, m_roiUiController, m_detectionUiController, m_configController,
        m_profileManager, m_displayModeManager, m_pipelineResultHandler, this);
    connect(m_session, &InspectionSession::imageSessionChanged, m_workspace, &DetectionWorkspace::restoreTabVisibility);
    connect(m_workspace, &DetectionWorkspace::refreshRequested, m_session, &InspectionSession::requestRefresh);
    connect(m_session, &InspectionSession::statusMessage, ui->statusbar, &QStatusBar::showMessage);
    setupConnections();
    setupControllerConnections();
    m_mqttManager = new MqttManager(this);
    m_remoteController = new RemoteInspectionController(m_mqttManager, m_autoDetectionController,
        &m_roiManager, m_pipelineManager, m_tabManager, this);
    connect(m_remoteController, &RemoteInspectionController::refreshRequested, m_session, &InspectionSession::requestRefresh);
    connect(m_remoteController, &RemoteInspectionController::startDetectionRequested, this, &MainWindow::startAutoDetection);
    connect(m_remoteController, &RemoteInspectionController::statusMessage, this,
        [this](const QString& text) { m_toast->showMessage(text); });
    m_mqttManager->setupLogging();
    if (mqttConfig) m_mqttManager->initialize(*mqttConfig);
    else m_mqttManager->initializeFromConfig();
}

MainWindow::~MainWindow()
{
    stopAllAsyncOperations();
    // Destroy central widgets while their borrowed services and ROI state live.
    // QObject's base destructor otherwise runs after MainWindow's members.
    delete takeCentralWidget();
    delete ui;
}

void MainWindow::stopAllAsyncOperations()
{
    m_isDestroying = true;
    m_remoteController->shutdown();
    m_workspace->stopVideo();
    m_autoDetectionController->shutdown();
    m_session->shutdown();
}

void MainWindow::setupBasicInfrastructure()
{
    setupUI();
    m_pageFadeAnimation = new QPropertyAnimation(this);
    m_pageFadeAnimation->setPropertyName("opacity");
    m_pageFadeAnimation->setDuration(280);
    m_pageFadeAnimation->setStartValue(0.0);
    m_pageFadeAnimation->setEndValue(1.0);
    m_pageFadeAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // 系统监控
    m_systemMonitor->setupWithStatusBar(ui->statusbar, AppConstants::SYSTEM_MONITOR_INTERVAL_MS);

    // Pipeline 耗时标签（StatusBar 永久显示，最左侧）
    m_timingLabel = new QLabel(this);
    m_timingLabel->setTextFormat(Qt::RichText);
    m_timingLabel->setText("<img src=':/icons/pipeline.png' width='16' height='16' style='vertical-align: middle;'> Pipeline: -- ms");
    ui->statusbar->insertPermanentWidget(0, m_timingLabel);
    // 计时由 PipelineResultHandler::pipelineTimingUpdated 信号驱动（实时更新）

    // Initialize LogPage (creates spdlog sinks: UI + rotating file)
    ui->page_log->initialize();
    connect(ui->page_log, &LogPage::requestGoHome, this, &MainWindow::onHomeClicked);
}

void MainWindow::setupUI()
{
    m_view = static_cast<ImageView*>(ui->graphicsView);
    QFile file(":/style.qss");
    if (file.open(QFile::ReadOnly)) {
        qApp->setStyleSheet(QLatin1String(file.readAll()));
    }

    ui->verticalLayout_imagePanel->insertWidget(0, new ImageViewControls(m_view, ui->widget_imagePanel));
    ui->splitter_main->setStretchFactor(0, 0);
    ui->splitter_main->setStretchFactor(1, 1);
    ui->splitter_main->setStretchFactor(2, 0);
    ui->splitter_main->setSizes({260, 760, 380});
    ui->tabWidget->setUsesScrollButtons(true);
    ui->tabWidget->tabBar()->setExpanding(false);

    // Toast 浮标（叠加在图像视图上）
    m_toast = new ToastNotification(m_view);
}

void MainWindow::setupConnections()
{
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(ui->btn_pipelineConfig, &QPushButton::clicked, m_workspace, &DetectionWorkspace::openPipelineConfig);
    connect(ui->btn_addROI, &QPushButton::clicked, m_roiUiController, &RoiUiController::onAddRoiClicked);
    connect(ui->btn_deleteROI, &QPushButton::clicked, m_roiUiController, &RoiUiController::onDeleteRoiClicked);
    connect(ui->btn_switchROI, &QPushButton::clicked, m_roiUiController, &RoiUiController::onSwitchRoiClicked);
    connect(ui->btn_Log, &QPushButton::clicked, this, &MainWindow::onLogClicked);
    connect(ui->btn_Home, &QPushButton::clicked, this, &MainWindow::onHomeClicked);
    connect(ui->btn_saveConfig, &QPushButton::clicked, this, &MainWindow::onSaveConfigClicked);
    connect(ui->btn_importConfig, &QPushButton::clicked, this, &MainWindow::onImportConfigClicked);
    connect(ui->btn_startAutoDetection, &QPushButton::clicked, this, &MainWindow::startAutoDetection);
    connect(ui->btn_stopAutoDetection, &QPushButton::clicked, m_autoDetectionController, &AutoDetectionController::stopDetection);
    connect(ui->btn_cloudPlatform, &QPushButton::clicked, m_cloudDashboardManager, &CloudDashboardManager::launch);
    connect(ui->btn_saveToProfile, &QPushButton::clicked, m_profileController, &ProfileController::saveToProfile);
    connect(ui->btn_loadFromProfile, &QPushButton::clicked, m_profileController, &ProfileController::loadFromProfile);
    connect(ui->btn_importFolder, &QPushButton::clicked, this, &MainWindow::onImportFolderClicked);

    connect(m_fileManager, &FileManager::imageLoaded, this,
        [this](const cv::Mat& image, const QString& path) { m_session->loadImage(image, path); });
    connect(m_fileManager, &FileManager::imageSaved, this, [](const QString& path) {
        spdlog::info("图像保存成功: {}", path.toStdString());
    });
    connect(m_fileManager, &FileManager::errorOccurred, this, [this](const QString& message) {
        spdlog::error("{}", message.toStdString());
        QMessageBox::warning(this, "错误", message);
    });

    // ImageView信号
    connect(m_view, &ImageView::pixelInfoChanged, this, [this](int x, int y, const QColor &color, int gray) {
        ui->statusbar->showMessage(
            QString("X=%1 Y=%2  R=%3 G=%4 B=%5  Gray=%6").arg(x).arg(y)
                .arg(color.red()).arg(color.green()).arg(color.blue()).arg(gray),
            AppConstants::PIXEL_INFO_TIMEOUT_MS);
    });
    connect(m_view, &ImageView::polygonDrawingPointAdded, this,
        [](const QString& type, const QPointF& point) {
            spdlog::info("[{}] 添加顶点: ({:.1f}, {:.1f})", type.toStdString(), point.x(), point.y());
        });
}

void MainWindow::animatePageSwitch(int toIndex)
{
    if (ui->stackedWidget_main->currentIndex() == toIndex) return;
    auto* page = ui->stackedWidget_main->widget(toIndex);
    if (!page) return;
    // One animation for the shell; repeated navigation cannot accumulate animations.
    m_pageFadeAnimation->stop();
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(page->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);
    }
    ui->stackedWidget_main->setCurrentIndex(toIndex);
    m_pageFadeAnimation->setTargetObject(effect);
    m_pageFadeAnimation->start();
}

void MainWindow::setupManagers()
{
    m_tabManager = new TabManager(ui->tabWidget, m_pipelineManager, &m_roiManager,
                                   m_view, statusBar(), this);

    m_cloudDashboardManager = new CloudDashboardManager(m_toast, this);

    m_displayModeManager = new DisplayModeManager(ui->tabWidget, m_pipelineManager, m_view, this);

    m_profileManager = new ProfileManager(&m_roiManager, m_pipelineManager, this);

    m_imageListManager = new ImageListManager(m_roiManager, m_fileManager, this, this);
    m_imageListManager->init(ui->listWidget_images, ui->btn_addImage, ui->btn_removeImage);

}

void MainWindow::setupControllers()
{
    m_roiDetectionConfigController = new RoiDetectionConfigController(m_roiManager, this);
    m_roiUiController = new RoiUiController(m_roiManager, m_pipelineManager, m_view, ui->statusbar, this);
    m_roiUiController->setDetectionConfigController(m_roiDetectionConfigController);
    m_detectionUiController = new DetectionUiController(m_roiManager, ui->tabWidget, this);
    m_configController = new ConfigController(m_pipelineManager, m_roiManager, this);
    m_autoDetectionController = new AutoDetectionController(&m_roiManager, this);
    m_profileController = new ProfileController(m_profileManager, &m_roiManager,
                                                m_pipelineManager, m_tabManager,
                                                m_roiUiController, m_toast, this, this);

    // 检测状态标签（创建并放置到状态栏）
    auto* detectionStatusLabel = new QLabel("");
    detectionStatusLabel->setObjectName("label_detectionStatus");
    detectionStatusLabel->setStyleSheet(
        "font-size: 11px; color: #6B7280; padding: 2px 4px;");
    ui->statusbar->insertPermanentWidget(1, detectionStatusLabel);
    m_autoDetectionController->setupUiConnections(
        ui->btn_startAutoDetection, ui->btn_stopAutoDetection,
        ui->statusbar, detectionStatusLabel);
    m_roiUiController->setupTreeView(ui->treeView);
    m_roiUiController->setupDisplayConnections(m_tabManager);

}

void MainWindow::setupControllerConnections()
{
    connect(m_roiUiController, &RoiUiController::roiChanged, m_session, &InspectionSession::requestRefresh);
    connect(m_configController, &ConfigController::configApplied, this, [this] {
        m_roiManager.saveImagePipelineConfig(m_roiManager.getCurrentImageId(), m_pipelineManager->getConfigSnapshot());
        m_session->requestRefresh();
    });
    connect(m_profileController, &ProfileController::requestRefresh, m_session, &InspectionSession::requestRefresh);
    connect(m_autoDetectionController, &AutoDetectionController::detectionFinished, this, [this](const DetectionStats&) {
        m_toast->showMessage("批量检测结束");
    });
    connect(ui->btn_addDetection, &QPushButton::clicked, m_workspace, &DetectionWorkspace::addDetection);
    connect(ui->btn_deleteDetection, &QPushButton::clicked, this, [this] { m_workspace->deleteDetection(ui->treeView); });
}

void MainWindow::setupResultHandler()
{
    m_pipelineResultHandler = new PipelineResultHandler(this);
    m_pipelineResultHandler->setDependencies(m_tabManager, &m_roiManager, m_view, m_pipelineManager);

    connect(m_pipelineManager->scheduler(), &PipelineScheduler::finished,
            m_pipelineResultHandler, &PipelineResultHandler::onPipelineResult);

    connect(m_pipelineManager->scheduler(), &PipelineScheduler::processingChanged,
            this, [this](bool processing) {
        if (processing) {
            ui->statusbar->showMessage("正在处理...");
        }
    });

    connect(m_pipelineResultHandler, &PipelineResultHandler::statusMessage,
            ui->statusbar, &QStatusBar::showMessage);

    connect(m_pipelineResultHandler, &PipelineResultHandler::pipelineTimingUpdated,
            this, [this](double totalMs) {
        m_timingLabel->setText(QString("Pipeline: %1 ms").arg(totalMs, 0, 'f', 1));
    });
}

void MainWindow::onTabChanged(int index)
{
    if (m_isDestroying) return;
    if (m_roiManager.getFullImage().empty()) return;

    m_displayModeManager->onTabChanged(index);
}

void MainWindow::onLogClicked()
{
    animatePageSwitch(1);
}

void MainWindow::onHomeClicked()
{
    animatePageSwitch(0);
}

void MainWindow::onSaveConfigClicked()  { m_configController->saveConfig(this); }
void MainWindow::onImportConfigClicked() { m_configController->loadConfig(this); }

void MainWindow::startAutoDetection()
{
    m_roiUiController->saveCurrentRoiPipelineConfig();
    m_autoDetectionController->startDetection();
}

void MainWindow::onImportFolderClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择图片文件夹", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList imported = m_roiManager.importImagesFromFolder(dir);
    QApplication::restoreOverrideCursor();

    if (imported.isEmpty()) {
        QMessageBox::information(this, "导入结果", "未导入任何图片。\n请确保文件夹中含有支持格式: .jpg, .png, .bmp, .tiff");
    } else {
        m_toast->showMessage(QString("已导入 %1 张图片").arg(imported.size()));
        spdlog::info("[MainWindow] 从文件夹导入 {} 张图片: {}", imported.size(), dir.toStdString());
    }
}

