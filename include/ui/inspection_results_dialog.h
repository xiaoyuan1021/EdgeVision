#pragma once

#include <QDialog>
#include "data/inspection_record.h"
#include "data/inspection_report_exporter.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

class InspectionResultsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit InspectionResultsDialog(QWidget* parent = nullptr);
    void setRecords(QList<InspectionRecord> records);
    bool exportReport(const QString& path, InspectionReportExporter::Format format,
                      QString* error = nullptr) const;

signals:
    void imageActivated(const QString& imageId);

private:
    void rebuildTree();
    void applyFilters();
    void chooseExportPath(InspectionReportExporter::Format format);

    QList<InspectionRecord> m_records;
    QLineEdit* m_search;
    QComboBox* m_filter;
    QTreeWidget* m_tree;
    QLabel* m_summary;
    QLabel* m_empty;
    QPushButton* m_exportCsv;
    QPushButton* m_exportJson;
};
