#include "controllers/detection_ui_controller.h"
#include "controllers/roi_ui_controller.h"
#include "config/detection_config_types.h"
#include "logger.h"
#include "widgets/tab_manager.h"
#include "widgets/object_detection_tab_widget.h"
#include "widgets/ocr_tab_widget.h"

#include <QMessageBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSignalBlocker>

DetectionUiController::DetectionUiController(
    RoiManager& roiManager,
    QTabWidget* tabWidget,
    QObject* parent)
    : QObject(parent)
    , m_roiManager(roiManager)
    , m_tabWidget(tabWidget)
{
}

void DetectionUiController::onAddDetectionClicked(const QString& roiId)
{
    spdlog::debug("[DEBUG] onAddDetectionClicked: roiId = {}", roiId.toStdString());
    
    // 检查ROI ID是否有效
    if (roiId.isEmpty()) {
        QMessageBox::warning(nullptr, "警告", "请先在左侧列表中选择一个ROI");
        return;
    }
    
    // 获取ROI配置
    RoiConfig* roi = m_roiManager.getRoiConfig(roiId);
    if (!roi) {
        spdlog::debug("[DEBUG] onAddDetectionClicked: ROI not found for ID = {}", roiId.toStdString());
        QMessageBox::warning(nullptr, "警告", "未找到选中的ROI");
        return;
    }
    
    spdlog::debug("[DEBUG] onAddDetectionClicked: Found ROI = {}, ID = {}", roi->roiName.toStdString(), roi->roiId.toStdString());
    
    // 创建对话框
    QDialog dialog;
    dialog.setWindowTitle("添加检测项");
    dialog.setMinimumWidth(250);
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    // 创建布局
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);
    
    // 添加标签
    QLabel* label = new QLabel("请选择检测项类型:", &dialog);
    layout->addWidget(label);
    
    // 创建ComboBox
    QComboBox* comboBox = new QComboBox(&dialog);
    comboBox->addItem("自定义Pipeline", static_cast<int>(DetectionType::Custom));
    comboBox->addItem("Blob分析", static_cast<int>(DetectionType::Blob));
    comboBox->addItem("条码识别", static_cast<int>(DetectionType::Barcode));
    comboBox->addItem("直线检测", static_cast<int>(DetectionType::Line));
    comboBox->addItem("OCR识别", static_cast<int>(DetectionType::Ocr));
    comboBox->addItem("目标检测", static_cast<int>(DetectionType::ObjectDetection));
    comboBox->addItem("视频检测", static_cast<int>(DetectionType::VideoDetection));
    comboBox->setMinimumWidth(150);
    layout->addWidget(comboBox);
    
    // 添加按钮
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    
    // 连接按钮信号
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    // 显示对话框（自动居中）
    if (dialog.exec() == QDialog::Accepted) {
        int index = comboBox->currentIndex();
        if (index >= 0) {
            DetectionType type = static_cast<DetectionType>(comboBox->currentData().toInt());
            
            // 创建检测项
            QString typeName = detectionTypeToString(type);
            DetectionItem item(typeName, type);
            
            spdlog::debug("[DEBUG] onAddDetectionClicked: Adding detection item to ROI = {}", roi->roiName.toStdString());
            
            // 添加到ROI
            roi->addDetectionItem(item);
            
            emit detectionChanged();
            emit detectionAdded(roiId, item.itemId);
            
            spdlog::info("已为ROI {} 添加检测项: {}", roi->roiName.toStdString(), typeName.toStdString());
        }
    }
}

void DetectionUiController::onDeleteDetectionClicked(const QString& roiId, const QString& detectionId)
{
    if (roiId.isEmpty() || detectionId.isEmpty()) {
        QMessageBox::warning(nullptr, "警告", "请先选择要删除的检测项");
        return;
    }

    RoiConfig* roi = m_roiManager.getRoiConfig(roiId);
    if (!roi) return;

    roi->removeDetectionItem(detectionId);
    emit detectionChanged();
    emit tabVisibilityUpdateNeeded();

    // 触发Pipeline重新执行，刷新画布显示
    if (m_triggerPipeline) {
        m_triggerPipeline();
    }

    spdlog::info("已删除检测项: {}", detectionId.toStdString());
}

void DetectionUiController::onDetectionItemSelected(const QString& roiId, const QString& detectionId,
                                                    TabManager* tabManager, PipelineManager* pipelineManager)
{
    auto* roi = m_roiManager.getRoiConfig(roiId);
    if (!roi) return;
    const auto* detection = roi->getDetectionItem(detectionId);
    if (!detection) return;

    switchToTabConfig(TabConfig::getConfigForType(detection->type));

    const auto selectedConfig = detection->resolvedPipeline(roi->pipelineConfig);
    if (detection->type == DetectionType::Ocr) {
        OcrDetectionConfig ocr;
        ocr.fromJson(detection->config);
        if (auto* tab = tabManager->getTabAs<OcrTabWidget>("文字识别"))
            tab->setJudgmentConfig(ocr.expectedText, ocr.matchExact);
    } else if (detection->type == DetectionType::ObjectDetection) {
        if (auto* tab = tabManager->getTabAs<ObjectDetectionTabWidget>("目标检测"))
            tab->setDetectionConfig(selectedConfig.objectDetection);
    }

    pipelineManager->setConfig(selectedConfig);
    pipelineManager->rebuildPipeline();
    roi->pipelineConfig = selectedConfig;
    for (auto* widget : tabManager->allTabs()) {
        if (auto* configurable = dynamic_cast<IConfigurableTab*>(widget))
            configurable->loadFromConfig(selectedConfig);
    }
}

void DetectionUiController::switchToTabConfig(const TabConfig& config)
{
    if (!m_tabWidget) return;

    // 先确保所有需要的Tab都已创建（懒加载）
    for (const QString& tabName : config.tabNames) {
        emit ensureTabNeeded(tabName);
    }

    // 阻塞信号，避免多次 setTabVisible / setCurrentIndex 触发中间状态的 onTabChanged
    const QSignalBlocker blocker(m_tabWidget);

    // 以页面名称匹配可见性；Tab 的实际顺序可以由用户拖动。
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        bool shouldShow = config.tabNames.contains(m_tabWidget->tabText(i));
        m_tabWidget->setTabVisible(i, shouldShow);
    }

    // 按 config.tabNames 的顺序重排可见的Tab
    auto* tabBar = m_tabWidget->tabBar();
    for (int desired = 0; desired < config.tabNames.size(); ++desired) {
        for (int cur = desired; cur < tabBar->count(); ++cur) {
            if (tabBar->tabText(cur) == config.tabNames[desired]) {
                if (cur != desired) {
                    tabBar->moveTab(cur, desired);
                }
                break;
            }
        }
    }

    // 切换到第一个可见的Tab
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->isTabVisible(i)) {
            m_tabWidget->setCurrentIndex(i);
            break;
        }
    }


}

// ========== UI信号连接 ==========

bool DetectionUiController::handleDeleteFromTree(QTreeWidget* treeWidget)
{
    if (!treeWidget) return false;

    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem) {
        QMessageBox::warning(nullptr, "警告", "请先选择要删除的检测项");
        return false;
    }

    if (currentItem->data(0, Qt::UserRole + 1).toString() == "detection") {
        QTreeWidgetItem* parentItem = currentItem->parent();
        if (parentItem) {
            onDeleteDetectionClicked(
                parentItem->data(0, Qt::UserRole).toString(),
                currentItem->data(0, Qt::UserRole).toString());
            return true;
        }
    } else {
        QMessageBox::warning(nullptr, "警告", "请先选择要删除的检测项");
    }
    return false;
}

void DetectionUiController::setupConnections(
    RoiUiController* roiController, std::function<void(const QString&)> ensureTabFunc,
    std::function<void()> triggerPipeline)
{
    m_triggerPipeline = triggerPipeline;
    connect(this, &DetectionUiController::detectionAdded,
            roiController, &RoiUiController::activateDetectionItem);

    connect(this, &DetectionUiController::detectionChanged,
            roiController, &RoiUiController::refreshRoiTreeView);
    if (ensureTabFunc) {
        connect(this, &DetectionUiController::ensureTabNeeded, this, std::move(ensureTabFunc));
    }

}
