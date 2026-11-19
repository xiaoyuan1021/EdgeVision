#include "ui/inspection_results_dialog.h"
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <utility>

namespace {
enum ItemRole { ImageIdRole = Qt::UserRole, KeyRole, SearchRole, StatusRole };
enum Filter { All, Passed, Failed, NotExecuted };

QString statusText(InspectionStatus status)
{
    switch (status) {
    case InspectionStatus::Passed: return QStringLiteral("通过");
    case InspectionStatus::Failed: return QStringLiteral("失败");
    case InspectionStatus::Skipped: return QStringLiteral("已跳过");
    case InspectionStatus::Pending: return QStringLiteral("未执行");
    }
    return {};
}

void decorateStatus(QTreeWidgetItem* item, InspectionStatus status)
{
    item->setText(1, statusText(status));
    const QColor color = status == InspectionStatus::Failed ? QColor("#A82A32")
        : status == InspectionStatus::Passed ? QColor("#226B43") : QColor("#566579");
    item->setForeground(1, color);
}

void identify(QTreeWidgetItem* item, const QString& imageId, const QString& key)
{
    item->setData(0, ImageIdRole, imageId);
    item->setData(0, KeyRole, key);
    item->setToolTip(0, item->text(0));
    item->setToolTip(2, item->text(2));
}

QString regionDetails(const RoiDetectionResult& roi)
{
    QStringList details;
    if (!roi.failReason.isEmpty()) details << roi.failReason;
    if (!roi.ocrText.isEmpty()) details << QStringLiteral("文字：%1").arg(roi.ocrText);
    for (const auto& barcode : roi.barcodeResults)
        details << QStringLiteral("条码：%1").arg(barcode.data);
    return details.isEmpty() ? QStringLiteral("%1 个检测项").arg(roi.itemResults.size()) : details.join(" | ");
}
}

InspectionResultsDialog::InspectionResultsDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName("inspectionResultsDialog");
    setWindowTitle(QStringLiteral("检测结果"));
    resize(1000, 620);
    setMinimumSize(700, 400);
    setStyleSheet("QDialog#inspectionResultsDialog { background: #F4F7FB; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_summary = new QLabel(this);
    m_summary->setObjectName("resultsSummaryLabel");
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto* filters = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName("resultsSearchEdit");
    m_search->setPlaceholderText(QStringLiteral("搜索图片、ROI、检测项或失败原因"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(QStringLiteral("搜索检测结果"));
    m_filter = new QComboBox(this);
    m_filter->setObjectName("resultsStatusFilter");
    m_filter->setAccessibleName(QStringLiteral("按图片检测状态筛选"));
    m_filter->addItem(QStringLiteral("全部状态"), All);
    m_filter->addItem(QStringLiteral("仅通过"), Passed);
    m_filter->addItem(QStringLiteral("仅失败"), Failed);
    m_filter->addItem(QStringLiteral("未执行 / 已跳过"), NotExecuted);
    filters->addWidget(m_search, 1);
    filters->addWidget(m_filter);
    layout->addLayout(filters);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("resultsTree");
    m_tree->setHeaderLabels({QStringLiteral("图片 / ROI / 检测项"), QStringLiteral("状态"),
        QStringLiteral("详情"), QStringLiteral("耗时 (ms)"), QStringLiteral("完成时间")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setColumnWidth(0, 245);
    m_tree->setColumnWidth(1, 80);
    m_tree->setColumnWidth(3, 90);
    m_tree->setColumnWidth(4, 160);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);

    m_empty = new QLabel(this);
    m_empty->setObjectName("resultsEmptyLabel");
    m_empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_empty);

    auto* footer = new QHBoxLayout;
    auto* hint = new QLabel(QStringLiteral("导出包含整个批次，不受搜索和筛选影响。\n双击结果可定位对应图片。"), this);
    hint->setWordWrap(true);
    footer->addWidget(hint, 1);
    m_exportCsv = new QPushButton(QStringLiteral("导出 CSV…"), this);
    m_exportCsv->setObjectName("exportCsvButton");
    m_exportCsv->setAutoDefault(false);
    m_exportJson = new QPushButton(QStringLiteral("导出 JSON…"), this);
    m_exportJson->setObjectName("exportJsonButton");
    m_exportJson->setAutoDefault(false);
    footer->addWidget(m_exportCsv);
    footer->addWidget(m_exportJson);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    close->setAutoDefault(false);
    footer->addWidget(close);
    layout->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &InspectionResultsDialog::applyFilters);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &InspectionResultsDialog::applyFilters);
    connect(m_exportCsv, &QPushButton::clicked, this,
        [this] { chooseExportPath(InspectionReportExporter::Format::Csv); });
    connect(m_exportJson, &QPushButton::clicked, this,
        [this] { chooseExportPath(InspectionReportExporter::Format::Json); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const auto imageId = item->data(0, ImageIdRole).toString();
        if (!imageId.isEmpty()) emit imageActivated(imageId);
    });
    setRecords({});
}

void InspectionResultsDialog::setRecords(QList<InspectionRecord> records)
{
    m_records = std::move(records);
    rebuildTree();
}

void InspectionResultsDialog::rebuildTree()
{
    const auto selected = m_tree->currentItem()
        ? m_tree->currentItem()->data(0, KeyRole).toString() : QString{};
    QSet<QString> collapsed;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if (!(*it)->isExpanded()) collapsed.insert((*it)->data(0, KeyRole).toString());
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    int passed = 0, failed = 0, pending = 0, skipped = 0;
    for (qsizetype index = 0; index < m_records.size(); ++index) {
        const auto& record = m_records[index];
        const auto& result = record.result;
        switch (record.status) {
        case InspectionStatus::Passed: ++passed; break;
        case InspectionStatus::Failed: ++failed; break;
        case InspectionStatus::Pending: ++pending; break;
        case InspectionStatus::Skipped: ++skipped; break;
        }
        const QString key = result.imageId.isEmpty() ? QString::number(index) : result.imageId;
        auto* image = new QTreeWidgetItem(m_tree, {result.imageName.isEmpty() ? key : result.imageName});
        image->setText(2, result.failReason.isEmpty() ? result.imagePath : result.failReason);
        if (record.status == InspectionStatus::Passed || record.status == InspectionStatus::Failed)
            image->setText(3, QString::number(record.elapsedMs));
        if (record.completedAtMs > 0)
            image->setText(4, QDateTime::fromMSecsSinceEpoch(record.completedAtMs).toString("yyyy-MM-dd HH:mm:ss"));
        decorateStatus(image, record.status);
        identify(image, result.imageId, key);
        image->setData(0, StatusRole, static_cast<int>(record.status));
        QFont font = image->font(0);
        font.setBold(true);
        image->setFont(0, font);
        QStringList searchable{result.imageName, result.imageId, result.imagePath, result.failReason};
        for (const auto& roi : result.roiResults) {
            auto* region = new QTreeWidgetItem(image, {roi.roiName.isEmpty() ? roi.roiId : roi.roiName});
            region->setText(2, regionDetails(roi));
            decorateStatus(region, roi.passed ? InspectionStatus::Passed : InspectionStatus::Failed);
            const auto regionKey = key + "/" + roi.roiId;
            identify(region, result.imageId, regionKey);
            searchable << roi.roiName << roi.roiId << roi.failReason << roi.ocrText;
            for (const auto& barcode : roi.barcodeResults) searchable << barcode.data;
            for (const auto& item : roi.itemResults) {
                auto* detection = new QTreeWidgetItem(region, {item.itemName.isEmpty() ? item.itemId : item.itemName});
                detection->setText(2, item.failReason.isEmpty() ? item.detectionType : item.failReason);
                decorateStatus(detection, item.passed ? InspectionStatus::Passed : InspectionStatus::Failed);
                identify(detection, result.imageId, regionKey + "/" + item.itemId);
                searchable << item.itemName << item.itemId << item.detectionType << item.failReason;
            }
        }
        image->setData(0, SearchRole, searchable.join('\n'));
    }
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const auto key = (*it)->data(0, KeyRole).toString();
        (*it)->setExpanded(!collapsed.contains(key));
        if (key == selected) m_tree->setCurrentItem(*it);
    }
    m_summary->setText(QStringLiteral("共 %1 张图片  ·  通过 %2  ·  失败 %3  ·  未执行 %4  ·  已跳过 %5")
        .arg(m_records.size()).arg(passed).arg(failed).arg(pending).arg(skipped));
    m_exportCsv->setEnabled(!m_records.isEmpty());
    m_exportJson->setEnabled(!m_records.isEmpty());
    applyFilters();
    m_tree->verticalScrollBar()->setValue(scroll);
}

void InspectionResultsDialog::applyFilters()
{
    const auto query = m_search->text().trimmed();
    const int filter = m_filter->currentData().toInt();
    int visible = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        const auto status = static_cast<InspectionStatus>(item->data(0, StatusRole).toInt());
        const bool accepted = filter == All
            || (filter == Passed && status == InspectionStatus::Passed)
            || (filter == Failed && status == InspectionStatus::Failed)
            || (filter == NotExecuted && (status == InspectionStatus::Pending || status == InspectionStatus::Skipped));
        const bool matches = item->data(0, SearchRole).toString().contains(query, Qt::CaseInsensitive);
        item->setHidden(!accepted || !matches);
        if (!item->isHidden()) ++visible;
    }
    m_empty->setText(m_records.isEmpty() ? QStringLiteral("暂无检测结果。运行批量检测后可在这里查看和导出。")
        : QStringLiteral("没有符合当前搜索或筛选条件的图片。"));
    m_empty->setVisible(visible == 0);
}

bool InspectionResultsDialog::exportReport(const QString& path,
    InspectionReportExporter::Format format, QString* error) const
{
    return InspectionReportExporter::save(path, m_records, format, error);
}

void InspectionResultsDialog::chooseExportPath(InspectionReportExporter::Format format)
{
    const bool csv = format == InspectionReportExporter::Format::Csv;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出整个批次"),
        csv ? "inspection-results.csv" : "inspection-results.json",
        csv ? QStringLiteral("CSV 文件 (*.csv)") : QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!exportReport(path, format, &error))
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    else
        QMessageBox::information(this, QStringLiteral("导出完成"),
            QStringLiteral("已导出全部 %1 张图片的检测记录。\n%2").arg(m_records.size()).arg(path));
}
