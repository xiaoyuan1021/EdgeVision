#include "widgets/object_detection_tab_widget.h"
#include "ui_object_detection_tab.h"
#include "logger.h"
#include "algorithm/object_detection_service.h"
#include "core/pipeline.h"
#include "controllers/roi_ui_controller.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <QFile>

ObjectDetectionTabWidget::ObjectDetectionTabWidget(IPipelineAccess* pipelineAccess, QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::ObjectDetectionTabForm)
    , m_pipeline(pipelineAccess)
{
    m_ui->setupUi(this);
    setupConnections();
    autoLoadDefaultModel();
}

ObjectDetectionTabWidget::~ObjectDetectionTabWidget()
{
    delete m_ui;
}

void ObjectDetectionTabWidget::setupConnections()
{
    connect(m_ui->btn_apply, &QPushButton::clicked, this, &ObjectDetectionTabWidget::onApplyClicked);
    connect(m_ui->btn_browseModel, &QPushButton::clicked, this, &ObjectDetectionTabWidget::onBrowseModel);
    connect(m_ui->btn_browseConfig, &QPushButton::clicked, this, &ObjectDetectionTabWidget::onBrowseConfig);
    
    // 置信度阈值滑块
    connect(m_ui->slider_confidenceThreshold, &QSlider::valueChanged, this, [this](int value) {
        float threshold = value / 100.0f;
        m_ui->label_confidenceValue->setText(QString::number(threshold, 'f', 2));
    });
    
    // NMS阈值滑块
    connect(m_ui->slider_nmsThreshold, &QSlider::valueChanged, this, [this](int value) {
        float threshold = value / 100.0f;
        m_ui->label_nmsValue->setText(QString::number(threshold, 'f', 2));
    });

    // 模型加载完成信号（仅更新状态，不自动触发检测）
    connect(this, &ObjectDetectionTabWidget::modelLoadFinished, this,
        [this](bool success, const QString& message) {
            m_ui->label_status->setText(message);
            if (success) {
                spdlog::info("[ObjectDetection] model loaded successfully");
            }
        });
}

void ObjectDetectionTabWidget::onBrowseModel()
{
    QString defaultDir = QString(PROJECT_ROOT_DIR) + "/resources/models";
    QString filePath = QFileDialog::getOpenFileName(this,
        "选择模型文件", defaultDir,
        "模型文件 (*.onnx *.prototxt *.pb *.cfg *.weights);;ONNX模型 (*.onnx);;所有文件 (*)");

    if (!filePath.isEmpty()) {
        m_ui->lineEdit_modelPath->setText(filePath);
        loadModelAsync();  // 路径变更后自动加载
    }
}

void ObjectDetectionTabWidget::onBrowseConfig()
{
    QString defaultDir = QString(PROJECT_ROOT_DIR) + "/resources/models";
    QString filePath = QFileDialog::getOpenFileName(this,
        "选择配置文件", defaultDir,
        "配置文件 (*.prototxt *.pbtxt *.cfg);;所有文件 (*)");

    if (!filePath.isEmpty()) {
        m_ui->lineEdit_configPath->setText(filePath);
        loadModelAsync();  // 配置变更后自动加载
    }
}

void ObjectDetectionTabWidget::autoLoadDefaultModel()
{
    // 默认模型路径：项目根目录/resources/models/model_pin/ort/pin.onnx
    QString defaultModelPath = QString(PROJECT_ROOT_DIR) + "/resources/models/model_pin/ort/pin.onnx";

    if (QFile::exists(defaultModelPath)) {
        m_ui->lineEdit_modelPath->setText(defaultModelPath);
        spdlog::debug("[ObjectDetection] auto-loading default model: {}", defaultModelPath.toStdString());

        // 延迟加载模型，等待UI完全初始化
        QTimer::singleShot(100, this, &ObjectDetectionTabWidget::loadModelAsync);
    } else {
        spdlog::debug("[ObjectDetection] default model not found: {}", defaultModelPath.toStdString());
    }
}

void ObjectDetectionTabWidget::loadModelAsync()
{
    const auto config = getDetectionConfig();
    const auto generation = ++m_loadGeneration;
    m_ui->label_status->setText("状态：正在加载模型...");
    m_ui->btn_apply->setEnabled(false);
    // Only value snapshots cross the worker boundary; closing the tab is safe.
    auto future = QtConcurrent::run([config]() -> QString {
        try {
            auto info = ObjectDetectionService::prepare(config);
            return "状态：模型已就绪 (" + info.backend + ")";
        } catch (const std::exception& ex) {
            return "加载失败: " + QString::fromUtf8(ex.what());
        }
    });
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation]() {
        const QString message = watcher->result();
        watcher->deleteLater();
        if (generation != m_loadGeneration) return;
        m_ui->btn_apply->setEnabled(true);
        emit modelLoadFinished(!message.startsWith("加载失败"), message);
    });
    watcher->setFuture(future);
}

void ObjectDetectionTabWidget::onApplyClicked()
{
    const auto config = getDetectionConfig();
    m_pipeline->updateConfig([config](PipelineConfig& pipeline) {
        pipeline.objectDetectionApplyEnabled = true;
        pipeline.objectDetection = config;
    });
    emit detectionConfigChanged();
}

void ObjectDetectionTabWidget::updateFromPipeline(const PipelineContext& context)
{
    updateDetectionResults(context.objectDetectionResults);
}

float ObjectDetectionTabWidget::getConfidenceThreshold() const
{
    return m_ui->slider_confidenceThreshold->value() / 100.0f;
}

float ObjectDetectionTabWidget::getNmsThreshold() const
{
    return m_ui->slider_nmsThreshold->value() / 100.0f;
}

ObjectDetectionConfig ObjectDetectionTabWidget::getDetectionConfig() const
{
    ObjectDetectionConfig config = m_renderedConfig;
    config.modelPath = m_ui->lineEdit_modelPath->text();
    config.configPath = m_ui->lineEdit_configPath->text();
    config.confidenceThreshold = getConfidenceThreshold();
    config.nmsThreshold = getNmsThreshold();
    config.expectedCount = m_ui->spinBox_expectedCount->value();
    return config;
}

void ObjectDetectionTabWidget::setDetectionConfig(const ObjectDetectionConfig& config)
{
    m_renderedConfig = config;
    ++m_loadGeneration;
    m_ui->btn_apply->setEnabled(true);
    QSignalBlocker blocker1(m_ui->lineEdit_modelPath);
    QSignalBlocker blocker2(m_ui->lineEdit_configPath);
    QSignalBlocker blocker3(m_ui->slider_confidenceThreshold);
    QSignalBlocker blocker4(m_ui->slider_nmsThreshold);
    QSignalBlocker blocker5(m_ui->spinBox_expectedCount);

    m_ui->lineEdit_modelPath->setText(config.modelPath);
    m_ui->lineEdit_configPath->setText(config.configPath);
    m_ui->slider_confidenceThreshold->setValue(static_cast<int>(config.confidenceThreshold * 100));
    m_ui->slider_nmsThreshold->setValue(static_cast<int>(config.nmsThreshold * 100));
    m_ui->spinBox_expectedCount->setValue(config.expectedCount);
    m_ui->label_confidenceValue->setText(QString::number(config.confidenceThreshold, 'f', 2));
    m_ui->label_nmsValue->setText(QString::number(config.nmsThreshold, 'f', 2));
}

void ObjectDetectionTabWidget::updateDetectionResults(const std::vector<DetectionResult>& results)
{
    // 清空表格
    m_ui->tableWidget_results->setRowCount(0);
    
    if (results.empty()) {
        m_ui->label_status->setText("状态：未检测到目标");
        return;
    }
    
    // 设置表格行数
    m_ui->tableWidget_results->setRowCount(static_cast<int>(results.size()));
    
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const DetectionResult& det = results[i];
        
        // 类别名称
        QString className = QString::fromStdString(det.className);
        m_ui->tableWidget_results->setItem(i, 0, new QTableWidgetItem(className));
        
        // 置信度
        QString confidence = QString::number(det.confidence, 'f', 3);
        m_ui->tableWidget_results->setItem(i, 1, new QTableWidgetItem(confidence));
        
        // 位置
        QString position = QString("(%1, %2) %3x%4")
            .arg(det.box.x).arg(det.box.y)
            .arg(det.box.width).arg(det.box.height);
        m_ui->tableWidget_results->setItem(i, 2, new QTableWidgetItem(position));
    }
    
    // 更新状态
    m_ui->label_status->setText(QString("状态：检测到 %1 个目标").arg(results.size()));
}

void ObjectDetectionTabWidget::connectSignals(const SignalContext& ctx,
                                               std::function<void()> onExecutePipeline,
                                               std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    Q_UNUSED(onConfigSaved);
    connect(this, &ObjectDetectionTabWidget::detectionConfigChanged,
            this, [onExecutePipeline]() { onExecutePipeline(); });

    // [NOTE] 所有配置变化时同步到DetectionItem.config
    auto syncToDetectionItem = [this, ctx]() {
        if (ctx.roiCtrl) {
            ctx.roiCtrl->updateObjectDetectionConfig(getDetectionConfig());
        }
    };

    connect(m_ui->lineEdit_modelPath, &QLineEdit::textChanged, this, syncToDetectionItem);
    connect(m_ui->lineEdit_configPath, &QLineEdit::textChanged, this, syncToDetectionItem);
    connect(m_ui->slider_confidenceThreshold, &QSlider::valueChanged, this, [this, syncToDetectionItem](int) { syncToDetectionItem(); });
    connect(m_ui->slider_nmsThreshold, &QSlider::valueChanged, this, [this, syncToDetectionItem](int) { syncToDetectionItem(); });
    connect(m_ui->spinBox_expectedCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, syncToDetectionItem](int) { syncToDetectionItem(); });
}