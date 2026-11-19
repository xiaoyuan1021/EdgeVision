#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include "ui/inspection_results_dialog.h"

namespace {
QList<InspectionRecord> records()
{
    InspectionRecord passed;
    passed.result.imageId = "passed-image";
    passed.result.imageName = QStringLiteral("合格样品");
    passed.result.imagePath = "images/passed.png";
    passed.status = InspectionStatus::Passed;
    passed.elapsedMs = 42;
    passed.completedAtMs = 1700000000000;

    InspectionRecord failed;
    failed.result.imageId = "failed-image";
    failed.result.imageName = QStringLiteral("针脚 B");
    failed.result.imagePath = "images/failed.png";
    failed.status = InspectionStatus::Failed;
    failed.elapsedMs = 63;
    failed.completedAtMs = 1700000001000;
    RoiDetectionResult roi("pin-roi", QStringLiteral("针脚区域"), "failed-image");
    DetectionItemResult item("blob-item", QStringLiteral("焊点"), "Blob");
    item.passed = false;
    item.failReason = QStringLiteral("数量不足：期望 4 个，实际 3 个");
    roi.addItemResult(item);
    roi.ocrText = "LOT-2411";
    failed.result.addRoiResult(roi);

    InspectionRecord pending;
    pending.result.imageId = "pending-image";
    pending.result.imageName = QStringLiteral("等待检测");
    InspectionRecord skipped;
    skipped.result.imageId = "skipped-image";
    skipped.result.imageName = QStringLiteral("未配置样品");
    skipped.result.failReason = QStringLiteral("未配置 ROI");
    skipped.status = InspectionStatus::Skipped;
    return {passed, failed, pending, skipped};
}

int visibleImages(QTreeWidget* tree)
{
    int count = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (!tree->topLevelItem(i)->isHidden()) ++count;
    return count;
}
}

class InspectionResultsTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QFile style(":/style.qss");
        QVERIFY(style.open(QFile::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }

    void emptyStateDisablesExport() {
        InspectionResultsDialog dialog;
        QVERIFY(!dialog.findChild<QPushButton*>("exportCsvButton")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton*>("exportJsonButton")->isEnabled());
        QVERIFY(!dialog.findChild<QLabel*>("resultsEmptyLabel")->isHidden());
    }

    void statusFiltersKeepPendingAndSkippedDistinctFromFailures() {
        InspectionResultsDialog dialog;
        dialog.setRecords(records());
        auto* tree = dialog.findChild<QTreeWidget*>("resultsTree");
        auto* filter = dialog.findChild<QComboBox*>("resultsStatusFilter");
        QCOMPARE(visibleImages(tree), 4);
        QCOMPARE(tree->topLevelItem(2)->text(1), QStringLiteral("未执行"));
        QCOMPARE(tree->topLevelItem(3)->text(1), QStringLiteral("已跳过"));
        QVERIFY(tree->topLevelItem(2)->text(3).isEmpty());
        filter->setCurrentIndex(1);
        QCOMPARE(visibleImages(tree), 1);
        QVERIFY(!tree->topLevelItem(0)->isHidden());
        filter->setCurrentIndex(2);
        QCOMPARE(visibleImages(tree), 1);
        QVERIFY(!tree->topLevelItem(1)->isHidden());
        filter->setCurrentIndex(3);
        QCOMPARE(visibleImages(tree), 2);
    }

    void searchKeepsRoiAndItemContextAndSurvivesRecordUpdates() {
        InspectionResultsDialog dialog;
        auto values = records();
        dialog.setRecords(values);
        auto* tree = dialog.findChild<QTreeWidget*>("resultsTree");
        auto* search = dialog.findChild<QLineEdit*>("resultsSearchEdit");
        search->setText(QStringLiteral("焊点"));
        QCOMPARE(visibleImages(tree), 1);
        auto* image = tree->topLevelItem(1);
        QCOMPARE(image->childCount(), 1);
        QCOMPARE(image->child(0)->child(0)->text(0), QStringLiteral("焊点"));
        tree->setCurrentItem(image->child(0)->child(0));
        values[0].elapsedMs = 100;
        dialog.setRecords(values);
        QCOMPARE(search->text(), QStringLiteral("焊点"));
        QCOMPARE(visibleImages(tree), 1);
        QVERIFY(tree->currentItem());
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("焊点"));
        search->setText("IMAGES/FAILED.PNG");
        QCOMPARE(visibleImages(tree), 1);
        search->setText("LOT-2411");
        QCOMPARE(visibleImages(tree), 1);
        QVERIFY(tree->topLevelItem(1)->child(0)->text(2).contains("LOT-2411"));
        search->setText("not-present");
        QCOMPARE(visibleImages(tree), 0);
        QVERIFY(!dialog.findChild<QLabel*>("resultsEmptyLabel")->isHidden());
    }

    void childActivationUsesImageIdentity() {
        InspectionResultsDialog dialog;
        dialog.setRecords(records());
        auto* tree = dialog.findChild<QTreeWidget*>("resultsTree");
        QSignalSpy activated(&dialog, &InspectionResultsDialog::imageActivated);
        tree->itemDoubleClicked(tree->topLevelItem(1)->child(0)->child(0), 0);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), QString("failed-image"));
    }

    void exportIncludesHiddenAndUnexecutedImages() {
        InspectionResultsDialog dialog;
        dialog.setRecords(records());
        dialog.findChild<QComboBox*>("resultsStatusFilter")->setCurrentIndex(2);
        dialog.findChild<QLineEdit*>("resultsSearchEdit")->setText("not-present");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("all-results.json");
        QString error;
        QVERIFY2(dialog.exportReport(path, InspectionReportExporter::Format::Json, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QFile::ReadOnly));
        const auto json = file.readAll();
        QVERIFY(json.contains("passed-image"));
        QVERIFY(json.contains("failed-image"));
        QVERIFY(json.contains("pending-image"));
        QVERIFY(json.contains("skipped-image"));
        QVERIFY(!dialog.exportReport(directory.path(), InspectionReportExporter::Format::Csv, &error));
        QVERIFY(!error.isEmpty());
    }

    void dialogRemainsUsableAtLaptopSize() {
        InspectionResultsDialog dialog;
        dialog.setRecords(records());
        dialog.resize(860, 520);
        dialog.show();
        QCoreApplication::processEvents();
        auto* tree = dialog.findChild<QTreeWidget*>("resultsTree");
        QVERIFY(tree->height() > 200);
        for (const auto* name : {"exportCsvButton", "exportJsonButton"}) {
            auto* button = dialog.findChild<QPushButton*>(name);
            QVERIFY(button->isVisible());
            QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint{}), button->size())));
        }
        const auto captureDirectory = qEnvironmentVariable("EDGEVISION_UI_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(dialog.grab().save(captureDirectory + "/inspection-results.png"));
        }
    }
};

QTEST_MAIN(InspectionResultsTest)
#include "inspection_results_test.moc"
