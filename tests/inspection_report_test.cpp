#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "data/inspection_report_exporter.h"

namespace {

InspectionRecord failedRecord()
{
    InspectionRecord record;
    record.status = InspectionStatus::Failed;
    record.completedAtMs = 1700000000123;
    record.elapsedMs = 37;
    record.result.imageId = "image-1";
    record.result.imageName = QStringLiteral("中文,\"零件\"\r\n第二行");
    record.result.imagePath = "C:/images/part.png";
    RoiDetectionResult roi("roi-1", QStringLiteral("引脚"), record.result.imageId);
    DetectionItemResult item("item-1", "count", "Blob");
    item.passed = false;
    item.failReason = "count,\"wrong\"\nexpected 2";
    item.detail = {{"actual", 1}, {"expected", 2}};
    roi.addItemResult(item);
    roi.regionCount = 1;
    RegionFeature feature;
    feature.area = 25;
    feature.bbox = {1, 2, 5, 5};
    roi.regionFeatures.push_back(feature);
    roi.barcodeResults.append({"QR", "123", QRectF(1, 2, 3, 4), 98, 90});
    roi.ocrText = QStringLiteral("标签");
    roi.ocrRegions.append({1, 2, 3, 4, roi.ocrText, 0.9});
    roi.matchedLineCount = 1;
    roi.totalLineCount = 3;
    record.result.addRoiResult(roi);
    return record;
}

// Parse quoted fields independently, including embedded line breaks.
QList<QStringList> parseCsv(const QByteArray& data)
{
    auto text = QString::fromUtf8(data);
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if (ch == '"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else quoted = !quoted;
        } else if (!quoted && (ch == ',' || ch == '\r' || ch == '\n')) {
            row.append(field);
            field.clear();
            if (ch != ',') {
                rows.append(row);
                row.clear();
                if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            }
        } else field += ch;
    }
    return rows;
}

} // namespace

class InspectionReportTest : public QObject
{
    Q_OBJECT
private slots:
    void csvPreservesTextHierarchyAndOriginalTime() {
        const auto record = failedRecord();
        const auto data = InspectionReportExporter::csv({record});
        QVERIFY(data.startsWith(QByteArray::fromHex("efbbbf")));
        const auto rows = parseCsv(data);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].size(), 19);
        QCOMPARE(rows[1].size(), rows[0].size());
        const auto& row = rows[1];
        QCOMPARE(row[0], record.result.imageId);
        QCOMPARE(row[1], record.result.imageName);
        QCOMPARE(row[2], record.result.imagePath);
        QCOMPARE(row[3], QString("FAIL"));
        QCOMPARE(row[4], QString("FAIL"));
        QCOMPARE(row[5], record.result.failReason);
        QCOMPARE(row[6], QString("roi-1"));
        QCOMPARE(row[7], QStringLiteral("引脚"));
        QCOMPARE(row[8], QString("FAIL"));
        QCOMPARE(row[9], record.result.roiResults[0].failReason);
        QCOMPARE(row[10], QString("item-1"));
        QCOMPARE(row[11], QString("count"));
        QCOMPARE(row[12], QString("Blob"));
        QCOMPARE(row[13], QString("FAIL"));
        QCOMPARE(row[14], record.result.roiResults[0].itemResults[0].failReason);
        QCOMPARE(row[15], QString("2023-11-14T22:13:20.123Z"));
        QCOMPARE(row[16], QString::number(record.completedAtMs));
        QCOMPARE(row[17], QString("37"));
        QCOMPARE(row[18], QString("1"));
        QCOMPARE(InspectionReportExporter::csv({record}), data);
    }

    void jsonRetainsMeasurementsAndItemDetails() {
        const auto record = failedRecord();
        const auto data = InspectionReportExporter::json({record});
        const auto document = QJsonDocument::fromJson(data);
        QVERIFY(document.isObject());
        QCOMPARE(document.object()["schemaVersion"].toInt(), 1);
        const auto exported = document.object()["records"].toArray()[0].toObject();
        QCOMPARE(exported["completedAtMs"].toInteger(), record.completedAtMs);
        QCOMPARE(exported["elapsedMs"].toInteger(), record.elapsedMs);
        QCOMPARE(exported["status"].toString(), QString("FAIL"));
        QCOMPARE(exported["round"].toInt(), 1);
        const auto result = exported["result"].toObject();
        QCOMPARE(result["imageId"].toString(), record.result.imageId);
        QCOMPARE(result["imagePath"].toString(), record.result.imagePath);
        QCOMPARE(result["imageName"].toString(), record.result.imageName);
        const auto roi = result["roiResults"].toArray()[0].toObject();
        QCOMPARE(roi["roiId"].toString(), QString("roi-1"));
        QCOMPARE(roi["regionCount"].toInt(), 1);
        QCOMPARE(roi["matchedLineCount"].toInt(), 1);
        QCOMPARE(roi["totalLineCount"].toInt(), 3);
        const auto item = roi["itemResults"].toArray()[0].toObject();
        QCOMPARE(item["itemId"].toString(), QString("item-1"));
        QCOMPARE(item["detail"].toObject(), record.result.roiResults[0].itemResults[0].detail);
        QCOMPARE(roi["regionFeatures"].toArray()[0].toObject()["bbox"].toObject()["width"].toInt(), 5);
        const auto barcode = roi["barcodeResults"].toArray()[0].toObject();
        QCOMPARE(barcode["data"].toString(), QString("123"));
        QCOMPARE(barcode["orientation"].toInt(), 90);
        QCOMPARE(barcode["location"].toObject()["height"].toInt(), 4);
        QCOMPARE(roi["ocrRegions"].toArray()[0].toObject()["text"].toString(), QStringLiteral("标签"));
        QCOMPARE(InspectionReportExporter::json({record}), data);
    }

    void pendingSkippedAndEmptyRoisAreNotLostOrMarkedFailed() {
        InspectionRecord pending;
        pending.result.imageId = "pending";
        InspectionRecord skipped;
        skipped.status = InspectionStatus::Skipped;
        skipped.result.imageId = "skipped";
        skipped.result.failReason = QStringLiteral("无启用的ROI");
        InspectionRecord passed;
        passed.status = InspectionStatus::Passed;
        passed.result.imageId = "empty-roi";
        passed.result.addRoiResult({"roi-empty", "empty", "empty-roi"});
        const QList<InspectionRecord> records{pending, skipped, passed};
        const auto rows = parseCsv(InspectionReportExporter::csv(records));
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[1][3], QString("PENDING"));
        QCOMPARE(rows[2][3], QString("SKIPPED"));
        QVERIFY(rows[1][4].isEmpty());
        QVERIFY(rows[2][4].isEmpty());
        QVERIFY(rows[1][15].isEmpty());
        QVERIFY(rows[1][16].isEmpty());
        QCOMPARE(rows[3][6], QString("roi-empty"));
        QCOMPARE(rows[3][8], QString("PASS"));
        QVERIFY(rows[3][10].isEmpty());
        const auto json = QJsonDocument::fromJson(InspectionReportExporter::json(records)).object();
        const auto exported = json["records"].toArray();
        QCOMPARE(exported.size(), 3);
        QVERIFY(exported[0].toObject()["result"].toObject()["passed"].isNull());
        QVERIFY(exported[1].toObject()["result"].toObject()["passed"].isNull());
        QVERIFY(exported[2].toObject()["result"].toObject()["passed"].toBool());
    }

    void exportsKeepOriginalAndRetriedRounds() {
        const auto first = failedRecord();
        auto retry = first;
        retry.round = 2;
        retry.status = InspectionStatus::Passed;
        retry.result.passed = true;
        const QList<InspectionRecord> records{first, retry};
        const auto rows = parseCsv(InspectionReportExporter::csv(records));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[1][0], rows[2][0]);
        QCOMPARE(rows[1][3], QString("FAIL"));
        QCOMPARE(rows[2][3], QString("PASS"));
        QCOMPARE(rows[1][18], QString("1"));
        QCOMPARE(rows[2][18], QString("2"));
        const auto json = QJsonDocument::fromJson(InspectionReportExporter::json(records)).object();
        const auto exported = json["records"].toArray();
        QCOMPARE(exported.size(), 2);
        QCOMPARE(exported[0].toObject()["round"].toInt(), 1);
        QCOMPARE(exported[1].toObject()["round"].toInt(), 2);
    }

    void saveWritesExactBytesAndReportsFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QList<InspectionRecord> records{failedRecord()};
        QString error = "old error";
        const auto path = directory.filePath("报告.csv");
        QVERIFY(InspectionReportExporter::save(path, records, InspectionReportExporter::Format::Csv, &error));
        QVERIFY(error.isEmpty());
        QFile csv(path);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QCOMPARE(csv.readAll(), InspectionReportExporter::csv(records));
        csv.close();
        QVERIFY(InspectionReportExporter::save(path, records, InspectionReportExporter::Format::Json, &error));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QCOMPARE(csv.readAll(), InspectionReportExporter::json(records));
        csv.close();
        QVERIFY(!InspectionReportExporter::save(directory.path(), records, InspectionReportExporter::Format::Csv, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!InspectionReportExporter::save(directory.filePath("missing/report.json"), records,
                                               InspectionReportExporter::Format::Json));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QCOMPARE(csv.readAll(), InspectionReportExporter::json(records));
    }
};

QTEST_GUILESS_MAIN(InspectionReportTest)
#include "inspection_report_test.moc"
