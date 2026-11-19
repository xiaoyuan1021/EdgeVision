#include "data/inspection_report_exporter.h"
#include <QDateTime>
#include <QTimeZone>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

namespace {

QString statusName(InspectionStatus status)
{
    switch (status) {
    case InspectionStatus::Pending: return QStringLiteral("PENDING");
    case InspectionStatus::Passed: return QStringLiteral("PASS");
    case InspectionStatus::Failed: return QStringLiteral("FAIL");
    case InspectionStatus::Skipped: return QStringLiteral("SKIPPED");
    }
    return {};
}

bool hasJudgment(const InspectionRecord& record)
{
    return record.status == InspectionStatus::Passed || record.status == InspectionStatus::Failed;
}

QString outcome(bool passed)
{
    return passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
}

QString completionTime(const InspectionRecord& record)
{
    return record.completedAtMs > 0
        ? QDateTime::fromMSecsSinceEpoch(record.completedAtMs, QTimeZone(QTimeZone::UTC)).toString(Qt::ISODateWithMs)
        : QString{};
}

void appendCsvRow(QByteArray& output, QStringList fields)
{
    for (auto& field : fields) {
        field.replace('"', QStringLiteral("\"\""));
        field = '"' + field + '"';
    }
    output += fields.join(',').toUtf8();
    output += "\r\n";
}

QJsonObject rectangle(double x, double y, double width, double height)
{
    return {{"x", x}, {"y", y}, {"width", width}, {"height", height}};
}

QJsonObject serializeRoi(const RoiDetectionResult& roi)
{
    auto object = roi.toJson();
    auto items = object["itemResults"].toArray();
    for (qsizetype i = 0; i < roi.itemResults.size(); ++i) {
        auto item = items[i].toObject();
        item["detail"] = roi.itemResults[i].detail;
        items[i] = item;
    }
    object["itemResults"] = items;

    QJsonArray features;
    for (const auto& feature : roi.regionFeatures) {
        features.append(QJsonObject{
            {"index", feature.index}, {"area", feature.area}, {"circularity", feature.circularity},
            {"centerX", feature.centerX}, {"centerY", feature.centerY},
            {"width", feature.width}, {"height", feature.height},
            {"bbox", rectangle(feature.bbox.x, feature.bbox.y, feature.bbox.width, feature.bbox.height)}});
    }
    object["regionFeatures"] = features;

    QJsonArray barcodes;
    for (const auto& barcode : roi.barcodeResults) {
        barcodes.append(QJsonObject{
            {"type", barcode.type}, {"data", barcode.data}, {"quality", barcode.quality},
            {"orientation", barcode.orientation},
            {"location", rectangle(barcode.location.x(), barcode.location.y(),
                                    barcode.location.width(), barcode.location.height())}});
    }
    object["barcodeResults"] = barcodes;

    QJsonArray regions;
    for (const auto& region : roi.ocrRegions) {
        regions.append(QJsonObject{
            {"x", region.x}, {"y", region.y}, {"width", region.width}, {"height", region.height},
            {"text", region.text}, {"confidence", region.confidence}});
    }
    object["ocrRegions"] = regions;
    return object;
}

} // namespace

QByteArray InspectionReportExporter::csv(const QList<InspectionRecord>& records)
{
    QByteArray output = QByteArray::fromHex("efbbbf");
    appendCsvRow(output, {QStringLiteral("图片ID"), QStringLiteral("图片名称"), QStringLiteral("图片路径"),
        QStringLiteral("执行状态"), QStringLiteral("图片结果"), QStringLiteral("图片失败原因"),
        QStringLiteral("ROI ID"), QStringLiteral("ROI名称"), QStringLiteral("ROI结果"), QStringLiteral("ROI失败原因"),
        QStringLiteral("检测项ID"), QStringLiteral("检测项名称"), QStringLiteral("检测类型"),
        QStringLiteral("检测项结果"), QStringLiteral("检测项失败原因"), QStringLiteral("完成时间(UTC)"),
        QStringLiteral("完成时间戳(ms)"), QStringLiteral("耗时(ms)"), QStringLiteral("检测轮次")});

    for (const auto& record : records) {
        const auto append = [&](const RoiDetectionResult* roi, const DetectionItemResult* item) {
            const bool judged = hasJudgment(record);
            appendCsvRow(output, {record.result.imageId, record.result.imageName, record.result.imagePath,
                statusName(record.status), judged ? outcome(record.result.passed) : QString{}, record.result.failReason,
                roi ? roi->roiId : QString{}, roi ? roi->roiName : QString{},
                judged && roi ? outcome(roi->passed) : QString{}, roi ? roi->failReason : QString{},
                item ? item->itemId : QString{}, item ? item->itemName : QString{},
                item ? item->detectionType : QString{}, judged && item ? outcome(item->passed) : QString{},
                item ? item->failReason : QString{}, completionTime(record),
                record.completedAtMs > 0 ? QString::number(record.completedAtMs) : QString{},
                judged ? QString::number(record.elapsedMs) : QString{}, QString::number(record.round)});
        };
        if (record.result.roiResults.isEmpty()) append(nullptr, nullptr);
        for (const auto& roi : record.result.roiResults) {
            if (roi.itemResults.isEmpty()) append(&roi, nullptr);
            for (const auto& item : roi.itemResults) append(&roi, &item);
        }
    }
    return output;
}

QByteArray InspectionReportExporter::json(const QList<InspectionRecord>& records)
{
    QJsonArray rows;
    for (const auto& record : records) {
        auto result = record.result.toJson();
        if (!hasJudgment(record)) result["passed"] = QJsonValue::Null;
        QJsonArray rois;
        for (const auto& roi : record.result.roiResults) rois.append(serializeRoi(roi));
        result["roiResults"] = rois;
        rows.append(QJsonObject{{"status", statusName(record.status)}, {"completedAtMs", record.completedAtMs},
            {"completedAt", completionTime(record)}, {"elapsedMs", record.elapsedMs},
            {"round", record.round}, {"result", result}});
    }
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"records", rows}}).toJson(QJsonDocument::Indented);
}

bool InspectionReportExporter::save(const QString& path, const QList<InspectionRecord>& records,
                                    Format format, QString* error)
{
    if (error) error->clear();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto data = format == Format::Csv ? csv(records) : json(records);
    if (file.write(data) != data.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
