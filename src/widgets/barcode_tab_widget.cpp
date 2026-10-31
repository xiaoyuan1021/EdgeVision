#include "widgets/barcode_tab_widget.h"
#include "ui_barcode_tab.h"
#include <QSignalBlocker>

BarcodeTabWidget::BarcodeTabWidget(IPipelineAccess* pipelineAccess,
                                   QWidget* parent)
    : QWidget(parent)
    , m_pipeline(pipelineAccess)
    , m_ui(new Ui::BarcodeTabForm)
{
    m_ui->setupUi(this);
    
    // 连接信号和槽
    connect(m_ui->chk_enableBarcode, &QCheckBox::toggled,
            this, &BarcodeTabWidget::onEnableBarcodeChanged);
    connect(m_ui->comboBox_codeType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BarcodeTabWidget::onCodeTypeChanged);
    connect(m_ui->spinBox_maxNum, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BarcodeTabWidget::onMaxNumChanged);
    connect(m_ui->btn_apply, &QPushButton::clicked,
            this, &BarcodeTabWidget::handleApply);
    
    // 初始化表格
    m_ui->tableWidget_results->setHorizontalHeaderLabels(
        QStringList() << "条码类型" << "数据" << "质量" << "位置");
    m_ui->tableWidget_results->horizontalHeader()->setStretchLastSection(true);
    
    // 初始禁用设置
    m_ui->groupBox_settings->setEnabled(false);
}

BarcodeTabWidget::~BarcodeTabWidget()
{
    delete m_ui;
}

void BarcodeTabWidget::setBarcodeConfig(const BarcodeConfig& config)
{
    m_draft = config;
    QSignalBlocker blocker1(m_ui->chk_enableBarcode);
    QSignalBlocker blocker2(m_ui->comboBox_codeType);
    QSignalBlocker blocker3(m_ui->spinBox_maxNum);
    m_ui->chk_enableBarcode->setChecked(config.enableBarcode);
    while (m_ui->comboBox_codeType->count() > 4) m_ui->comboBox_codeType->removeItem(4);
    int index = config.codeTypes == QStringList{"auto"} || config.codeTypes.isEmpty()
        ? 0 : m_ui->comboBox_codeType->findText(config.codeTypes.join(", "));
    if (index < 0) {
        m_ui->comboBox_codeType->addItem(config.codeTypes.join(", "), config.codeTypes);
        index = m_ui->comboBox_codeType->count() - 1;
    }
    m_ui->comboBox_codeType->setCurrentIndex(index);
    m_ui->spinBox_maxNum->setValue(config.maxNumSymbols);
    m_ui->groupBox_settings->setEnabled(config.enableBarcode);
}

BarcodeConfig BarcodeTabWidget::getBarcodeConfig() const
{
    return m_draft;
}

void BarcodeTabWidget::applyConfig()
{
    if (!m_pipeline) return;
    const auto draft = m_draft;
    m_pipeline->updateConfig([draft](PipelineConfig& config) { config.barcode = draft; });
    emit barcodeConfigChanged();
    if (!draft.enableBarcode) {
        m_ui->tableWidget_results->setRowCount(0);
        m_ui->label_status->setText("状态：未识别");
    }
}

void BarcodeTabWidget::updateFromPipeline(const PipelineContext& ctx)
{
    updateResultsTable(ctx.barcodeResults);
    updateStatus(ctx.barcodeStatus);
}

void BarcodeTabWidget::updateResultsTable(const QVector<BarcodeResult>& results)
{
    m_ui->tableWidget_results->setRowCount(results.size());
    
    for (int i = 0; i < results.size(); i++)
    {
        const BarcodeResult& result = results[i];
        
        // 条码类型
        QTableWidgetItem* typeItem = new QTableWidgetItem(result.type);
        m_ui->tableWidget_results->setItem(i, 0, typeItem);
        
        // 数据
        QTableWidgetItem* dataItem = new QTableWidgetItem(result.data);
        m_ui->tableWidget_results->setItem(i, 1, dataItem);
        
        // 质量
        QString qualityText;
        if (result.quality >= 0)
        {
            qualityText = QString::number(result.quality, 'f', 1) + "%";
        }
        else
        {
            qualityText = "N/A";
        }
        QTableWidgetItem* qualityItem = new QTableWidgetItem(qualityText);
        m_ui->tableWidget_results->setItem(i, 2, qualityItem);
        
        // 位置
        QString locationText = QString("(%1, %2) %3x%4")
            .arg(result.location.x(), 0, 'f', 0)
            .arg(result.location.y(), 0, 'f', 0)
            .arg(result.location.width(), 0, 'f', 0)
            .arg(result.location.height(), 0, 'f', 0);
        QTableWidgetItem* locationItem = new QTableWidgetItem(locationText);
        m_ui->tableWidget_results->setItem(i, 3, locationItem);
    }
}

void BarcodeTabWidget::updateStatus(const QString& status)
{
    m_ui->label_status->setText("状态：" + status);
}

void BarcodeTabWidget::onEnableBarcodeChanged(bool enabled)
{
    m_ui->groupBox_settings->setEnabled(enabled);
    m_draft.enableBarcode = enabled;
    emit draftEdited(m_draft);
}

void BarcodeTabWidget::onCodeTypeChanged(int index)
{
    if (index == 0) m_draft.codeTypes = {"auto"};
    else if (index >= 4) m_draft.codeTypes = m_ui->comboBox_codeType->itemData(index).toStringList();
    else m_draft.codeTypes = {m_ui->comboBox_codeType->currentText()};
    emit draftEdited(m_draft);
}

void BarcodeTabWidget::onMaxNumChanged(int value)
{
    m_draft.maxNumSymbols = value;
    emit draftEdited(m_draft);
}

void BarcodeTabWidget::handleApply()
{
    applyConfig();
    emit requestApplyBarcodeSettings();
}

void BarcodeTabWidget::connectSignals(const SignalContext& ctx,
                                      std::function<void()> onExecutePipeline,
                                      std::function<void()> onConfigSaved)
{
    if (!beginSignalConnections()) return;
    connect(this, &BarcodeTabWidget::draftEdited, this, [save = ctx.saveBarcodeDraft](const BarcodeConfig& config) {
        if (save) save(config);
    });
    connect(this, &BarcodeTabWidget::barcodeConfigChanged, this, [onConfigSaved] {
        if (onConfigSaved) onConfigSaved();
    });
    connect(this, &BarcodeTabWidget::requestApplyBarcodeSettings, this, [onExecutePipeline] {
        if (onExecutePipeline) onExecutePipeline();
    });
}

// ========== IConfigurableTab 接口实现 ==========

void BarcodeTabWidget::saveToConfig(PipelineConfig& config) const
{
    config.barcode = getBarcodeConfig();
}

void BarcodeTabWidget::loadFromConfig(const PipelineConfig& config)
{
    setBarcodeConfig(config.barcode);
}

// ========== ITabInitializable 接口实现 ==========

void BarcodeTabWidget::initializeTab(const TabInitContext& ctx)
{
    Q_UNUSED(ctx);
    if (m_pipeline) setBarcodeConfig(m_pipeline->getConfigSnapshot().barcode);
}
