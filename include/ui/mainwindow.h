#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "core/roi_manager.h"
#include "config/mqtt_config.h"
#include <optional>

class PipelineManager;
class MqttManager;
class ToastNotification;
class QPropertyAnimation;
class InspectionSession;
class DetectionWorkspace;
class RemoteInspectionController;

// 前向声明
class QLabel;
class ImageView;
class SystemMonitor;
class FileManager;
class CloudDashboardManager;
class DisplayModeManager;
class ProfileController;
class TabManager;
class ImageListManager;
class RoiUiController;
class RoiDetectionConfigController;
class DetectionUiController;
class ConfigController;
class AutoDetectionController;
class PipelineResultHandler;
class ProfileManager;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * 主窗口 - 负责UI交互和显示
 *
 * 职责：
 * 1. UI事件处理
 * 2. 图像显示
 * 3. 协调各模块工作
 */

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, std::optional<MqttConfig> mqttConfig = std::nullopt);
    ~MainWindow();

private slots:
    void onTabChanged(int index);
    void onLogClicked();
    void onHomeClicked();
    void onSaveConfigClicked();
    void onImportConfigClicked();
    void onImportFolderClicked();

private:
    // 初始化方法
    void setupBasicInfrastructure();
    void setupManagers();
    void setupControllers();
    void setupControllerConnections();

    void setupResultHandler();

    void setupUI();
    void setupConnections();

    /// 页面切换淡入淡出动画
    void animatePageSwitch(int toIndex);
    void stopAllAsyncOperations();

    Ui::MainWindow *ui;
    ImageView *m_view = nullptr;

    PipelineManager* m_pipelineManager = nullptr;
    RoiManager m_roiManager;
    SystemMonitor* m_systemMonitor = nullptr;
    FileManager* m_fileManager = nullptr;

    bool m_isDestroying = false;

    // Tab管理器
    TabManager* m_tabManager = nullptr;

    // Application coordinators
    InspectionSession* m_session = nullptr;
    DetectionWorkspace* m_workspace = nullptr;
    RemoteInspectionController* m_remoteController = nullptr;

    // Controller对象
    RoiUiController* m_roiUiController = nullptr;
    DetectionUiController* m_detectionUiController = nullptr;
    ConfigController* m_configController = nullptr;
    RoiDetectionConfigController* m_roiDetectionConfigController = nullptr;

    // 图片列表管理器
    ImageListManager* m_imageListManager = nullptr;

    // 自动检测控制器
    AutoDetectionController* m_autoDetectionController = nullptr;

    // Pipeline结果处理器
    PipelineResultHandler* m_pipelineResultHandler = nullptr;

    // MQTT 边云协同管理器
    MqttManager* m_mqttManager = nullptr;

    // 云平台看板管理器
    CloudDashboardManager* m_cloudDashboardManager = nullptr;

    // 显示模式管理器
    DisplayModeManager* m_displayModeManager = nullptr;

    // 检测方案管理器
    ProfileManager* m_profileManager = nullptr;
    // 方案控制器
    ProfileController* m_profileController = nullptr;

    // Toast 通知浮标
    ToastNotification* m_toast = nullptr;

    // 基准性能显示
    QLabel* m_timingLabel = nullptr;

    // 动画相关
    QPropertyAnimation* m_pageFadeAnimation = nullptr;
};

#endif // MAINWINDOW_H
