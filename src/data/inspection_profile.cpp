#include "data/inspection_profile.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <utility>
#include <opencv2/imgproc.hpp>

namespace {
bool isStorageName(const QString& name)
{
    if (name.trimmed().isEmpty() || name.endsWith('.') || name.endsWith(' ')) return false;
    for (const auto ch : name)
        if (ch.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(ch)) return false;
    return true;
}

bool optionalObject(const QJsonObject& object, const char* key)
{
    return !object.contains(key) || object[key].isObject();
}

QString profileFormatError(const QJsonObject& object)
{
    if (object["profileId"].toString().trimmed().isEmpty()) return QStringLiteral("缺少方案 ID");
    if (!isStorageName(object["profileName"].toString())) return QStringLiteral("方案名称无效");
    if (!object["roiTemplates"].isArray()) return QStringLiteral("ROI 模板列表缺失或格式错误");
    if (!optionalObject(object, "globalPipelineConfig")) return QStringLiteral("全局配置格式错误");
    const auto rois = object["roiTemplates"].toArray();
    if (!rois.isEmpty() && (object["refImageWidth"].toInt() <= 0 || object["refImageHeight"].toInt() <= 0))
        return QStringLiteral("参考图像尺寸无效");
    for (const auto& value : rois) {
        if (!value.isObject()) return QStringLiteral("ROI 模板必须是对象");
        const auto roi = value.toObject();
        if (!roi["relativeRect"].isObject()) return QStringLiteral("ROI 矩形缺失或格式错误");
        const auto rect = roi["relativeRect"].toObject();
        for (const auto* key : {"x", "y", "width", "height"})
            if (!rect[key].isDouble() || !std::isfinite(rect[key].toDouble()))
                return QStringLiteral("ROI 坐标必须是有限数值");
        if (rect["width"].toDouble() <= 0 || rect["height"].toDouble() <= 0)
            return QStringLiteral("ROI 尺寸必须大于零");
        if (!optionalObject(roi, "pipelineConfig")) return QStringLiteral("ROI 配置格式错误");
        if (!roi.contains("detectionItems")) continue;
        if (!roi["detectionItems"].isArray()) return QStringLiteral("检测项列表格式错误");
        for (const auto& detection : roi["detectionItems"].toArray()) {
            if (!detection.isObject()) return QStringLiteral("检测项必须是对象");
            const auto item = detection.toObject();
            if (!optionalObject(item, "config") || !optionalObject(item, "pipelineSnapshot"))
                return QStringLiteral("检测项配置格式错误");
        }
    }
    if (object.contains("templates") && !object["templates"].isArray())
        return QStringLiteral("模板库列表格式错误");
    QSet<QString> names;
    for (const auto& value : object["templates"].toArray()) {
        const auto name = value.toObject()["templateName"].toString();
        if (!isStorageName(name)) return QStringLiteral("模板名称无效");
        if (names.contains(name.toCaseFolded())) return QStringLiteral("模板名称重复");
        names.insert(name.toCaseFolded());
    }
    return {};
}
}

// ==================== TemplateEntry 实现 ====================

bool TemplateEntry::load(const QString& baseDir) {
    QString tplPath = baseDir + "/templates/" + templateName + ".tpl";

    QFile file(tplPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return false;

    try {
        cv::FileStorage fs(std::string(data.begin(), data.end()),
            cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!fs.isOpened()) return false;

        fs["templateImage"] >> templateImage;
        if (templateImage.empty()) return false;

        fs["matchMethod"] >> params.matchMethod;

        params.polygonPoints.clear();
        cv::FileNode pts = fs["polygonPoints"];
        if (pts.type() == cv::FileNode::SEQ) {
            for (const auto& pt : pts) {
                params.polygonPoints.append(QPointF(pt["x"], pt["y"]));
            }
        }
    } catch (const cv::Exception&) {
        return false;
    }

    templateFilePath = "templates/" + templateName + ".tpl";
    return true;
}

bool TemplateEntry::save(const QString& baseDir) {
    if (templateImage.empty()) return false;

    QDir dir(baseDir);
    if (!dir.exists("templates")) {
        dir.mkpath("templates");
    }

    QString tplPath = baseDir + "/templates/" + templateName + ".tpl";

    try {
        cv::FileStorage fs(".tpl",
            cv::FileStorage::WRITE | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_YAML);

        fs << "templateImage" << templateImage;
        fs << "matchMethod" << params.matchMethod;

        fs << "polygonPoints" << "[";
        for (const auto& pt : params.polygonPoints) {
            fs << "{:" << "x" << pt.x() << "y" << pt.y() << "}";
        }
        fs << "]";

        std::string content = fs.releaseAndGetString();

        QFile file(tplPath);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(content.data(), content.size());
        file.close();
    } catch (const cv::Exception&) {
        return false;
    }

    templateFilePath = "templates/" + templateName + ".tpl";
    return true;
}

QJsonObject TemplateEntry::toJson() const {
    QJsonObject obj;
    obj["templateName"] = templateName;
    obj["templateFilePath"] = templateFilePath;
    return obj;
}

void TemplateEntry::fromJson(const QJsonObject& obj) {
    templateName = obj["templateName"].toString();
    templateFilePath = obj["templateFilePath"].toString();
}

// ==================== RoiTemplate 实现 ====================

RoiTemplate RoiTemplate::fromRoiConfig(const RoiConfig& roiConfig, const QSize& refImageSize) {
    RoiTemplate rt;
    rt.name = roiConfig.roiName;
    rt.color = roiConfig.color;
    rt.pipelineConfig = roiConfig.pipelineConfig;
    rt.detectionItems = roiConfig.detectionItems;

    if (refImageSize.width() > 0 && refImageSize.height() > 0) {
        rt.relativeRect = QRectF(
            roiConfig.roiRect.x() / refImageSize.width(),
            roiConfig.roiRect.y() / refImageSize.height(),
            roiConfig.roiRect.width() / refImageSize.width(),
            roiConfig.roiRect.height() / refImageSize.height()
        );
    }

    return rt;
}

RoiConfig RoiTemplate::toRoiConfig(const QSize& currentImageSize) const {
    RoiConfig rc;
    rc.roiName = name;
    rc.color = color;
    rc.pipelineConfig = pipelineConfig;
    rc.detectionItems = detectionItems;
    rc.roiId = generateUniqueId("roi");

    rc.roiRect = QRectF(
        relativeRect.x() * currentImageSize.width(),
        relativeRect.y() * currentImageSize.height(),
        relativeRect.width() * currentImageSize.width(),
        relativeRect.height() * currentImageSize.height()
    );

    return rc;
}

QJsonObject RoiTemplate::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["color"] = color;

    QJsonObject rect;
    rect["x"] = relativeRect.x();
    rect["y"] = relativeRect.y();
    rect["width"] = relativeRect.width();
    rect["height"] = relativeRect.height();
    obj["relativeRect"] = rect;

    QJsonArray detections;
    for (const auto& item : detectionItems) {
        detections.append(item.toJson());
    }
    obj["detectionItems"] = detections;

    obj["pipelineConfig"] = pipelineConfig.toJson();

    return obj;
}

void RoiTemplate::fromJson(const QJsonObject& obj) {
    name = obj["name"].toString();
    color = obj["color"].toString("#FF6B6B");

    QJsonObject rect = obj["relativeRect"].toObject();
    relativeRect = QRectF(
        rect["x"].toDouble(), rect["y"].toDouble(),
        rect["width"].toDouble(), rect["height"].toDouble()
    );

    detectionItems.clear();
    QJsonArray detections = obj["detectionItems"].toArray();
    for (const auto& val : detections) {
        DetectionItem item;
        item.fromJson(val.toObject());
        detectionItems.append(item);
    }

    QJsonObject pipeConfig = obj["pipelineConfig"].toObject();
    if (!pipeConfig.isEmpty()) {
        pipelineConfig.fromJson(pipeConfig);
    }
}

// ==================== InspectionProfile 实现 ====================

void InspectionProfile::addTemplate(const TemplateEntry& entry) {
    // 同名模板替换
    for (int i = 0; i < templates.size(); ++i) {
        if (templates[i].templateName == entry.templateName) {
            templates[i] = entry;
            updatedAt = QDateTime::currentDateTime();
            return;
        }
    }
    templates.append(entry);
    updatedAt = QDateTime::currentDateTime();
}

bool InspectionProfile::removeTemplate(const QString& templateName) {
    for (int i = 0; i < templates.size(); ++i) {
        if (templates[i].templateName == templateName) {
            templates.removeAt(i);
            updatedAt = QDateTime::currentDateTime();
            return true;
        }
    }
    return false;
}

const TemplateEntry* InspectionProfile::findTemplate(const QString& templateName) const {
    for (const auto& t : templates) {
        if (t.templateName == templateName) {
            return &t;
        }
    }
    return nullptr;
}

QStringList InspectionProfile::templateNames() const {
    QStringList names;
    for (const auto& t : templates) {
        names.append(t.templateName);
    }
    return names;
}

QJsonObject InspectionProfile::toJson() const {
    QJsonObject obj;
    obj["profileId"] = profileId;
    obj["profileName"] = profileName;
    obj["description"] = description;
    obj["createdAt"] = createdAt.toString(Qt::ISODate);
    obj["updatedAt"] = updatedAt.toString(Qt::ISODate);
    obj["refImageWidth"] = referenceImageSize.width();
    obj["refImageHeight"] = referenceImageSize.height();

    // ROI模板列表
    QJsonArray roiArray;
    for (const auto& roi : roiTemplates) {
        roiArray.append(roi.toJson());
    }
    obj["roiTemplates"] = roiArray;

    // 模板库元数据（实际文件另存）
    QJsonArray tplArray;
    for (const auto& tpl : templates) {
        tplArray.append(tpl.toJson());
    }
    obj["templates"] = tplArray;

    // 全局Pipeline配置
    obj["globalPipelineConfig"] = globalPipelineConfig.toJson();

    return obj;
}

void InspectionProfile::fromJson(const QJsonObject& obj) {
    profileId = obj["profileId"].toString();
    profileName = obj["profileName"].toString();
    description = obj["description"].toString();
    createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
    updatedAt = QDateTime::fromString(obj["updatedAt"].toString(), Qt::ISODate);
    referenceImageSize = QSize(
        obj["refImageWidth"].toInt(0),
        obj["refImageHeight"].toInt(0)
    );

    roiTemplates.clear();
    QJsonArray roiArray = obj["roiTemplates"].toArray();
    for (const auto& val : roiArray) {
        RoiTemplate rt;
        rt.fromJson(val.toObject());
        roiTemplates.append(rt);
    }

    templates.clear();
    QJsonArray tplArray = obj["templates"].toArray();
    for (const auto& val : tplArray) {
        TemplateEntry te;
        te.fromJson(val.toObject());
        templates.append(te);
    }

    globalPipelineConfig.fromJson(obj["globalPipelineConfig"].toObject());
}

bool InspectionProfile::saveToDirectory(const QString& profilesDir) {
    QString dirPath = profilesDir + "/" + profileName;
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 保存 profile.json
    QFile file(dirPath + "/profile.json");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson());
    file.close();

    // 保存模板文件
    for (auto& tpl : templates) {
        tpl.save(dirPath);
    }

    return true;
}

bool InspectionProfile::loadFromDirectory(const QString& profileDir, QString* error) {
    if (error) error->clear();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QFile file(profileDir + "/profile.json");
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(file.errorString());
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) return fail(parseError.errorString());
    if (!doc.isObject()) return fail(QStringLiteral("方案文件根节点必须是对象"));
    const auto formatError = profileFormatError(doc.object());
    if (!formatError.isEmpty()) return fail(formatError);

    InspectionProfile candidate;
    candidate.fromJson(doc.object());
    const auto directoryName = QDir(profileDir).dirName();
    if (candidate.profileName.compare(directoryName, Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("方案名称与所在目录不一致"));
    candidate.profileName = directoryName;
    for (auto& tpl : candidate.templates)
        if (!tpl.load(profileDir)) return fail(QStringLiteral("模板文件缺失或损坏：%1").arg(tpl.templateName));

    // 整个方案及其模板均有效后才替换内存状态。
    *this = std::move(candidate);
    return true;
}

cv::Mat InspectionProfile::generateThumbnail(const cv::Mat& fullImage, int thumbWidth) const {
    if (fullImage.empty()) return cv::Mat();

    cv::Mat thumb;
    double scale = static_cast<double>(thumbWidth) / fullImage.cols;
    cv::resize(fullImage, thumb, cv::Size(), scale, scale, cv::INTER_AREA);
    return thumb;
}
