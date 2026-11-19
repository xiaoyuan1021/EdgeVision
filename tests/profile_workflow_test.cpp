#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "core/pipeline_manager.h"
#include "core/profile_manager.h"
#include "core/roi_manager.h"

namespace {
QJsonObject legacyProfile()
{
    const QJsonObject rect{{"x", 0.1}, {"y", 0.2}, {"width", 0.3}, {"height", 0.4}};
    const QJsonObject roi{{"name", "legacy ROI"}, {"relativeRect", rect}};
    return {{"profileId", "profile-test"}, {"profileName", "inspection"},
            {"refImageWidth", 100}, {"refImageHeight", 80},
            {"roiTemplates", QJsonArray{roi}}};
}

bool writeManifest(const QString& directory, const QByteArray& contents)
{
    if (!QDir().mkpath(directory)) return false;
    QFile file(directory + "/profile.json");
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

InspectionProfile existingProfile()
{
    InspectionProfile profile;
    profile.profileId = "existing-id";
    profile.profileName = "existing";
    profile.description = "keep this profile";
    profile.referenceImageSize = QSize(100, 80);
    RoiConfig roi("existing ROI", QRectF(5, 6, 20, 30));
    roi.pipelineConfig.enhance.brightness = 17;
    profile.roiTemplates.append(RoiTemplate::fromRoiConfig(roi, profile.referenceImageSize));
    TemplateEntry entry("existing template");
    entry.templateImage = cv::Mat(2, 3, CV_8UC1, cv::Scalar(73));
    entry.params.polygonPoints = {{0, 0}, {2, 0}, {2, 1}};
    profile.templates.append(entry);
    return profile;
}
}

class ProfileWorkflowTest : public QObject
{
    Q_OBJECT
private slots:
    void invalidProfilesPreserveLoadedDataAndCurrentRois_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("empty-file") << QByteArray();
        QTest::newRow("broken-json") << QByteArray("{\"profileId\":");
        QTest::newRow("array-root") << QByteArray("[]");
        QTest::newRow("empty-object") << QByteArray("{}");

        const auto add = [](const char* name, const QJsonObject& document) {
            QTest::newRow(name) << QJsonDocument(document).toJson();
        };
        const auto replaceField = [&](const char* name, const QString& key, const QJsonValue& value) {
            auto document = legacyProfile();
            document[key] = value;
            add(name, document);
        };
        auto missingId = legacyProfile();
        missingId.remove("profileId");
        add("missing-id", missingId);
        replaceField("empty-id", "profileId", "");
        replaceField("wrong-id-type", "profileId", 42);
        replaceField("empty-name", "profileName", "");
        replaceField("wrong-name-type", "profileName", 42);
        replaceField("directory-name-mismatch", "profileName", "other");

        auto missingReferenceWidth = legacyProfile();
        missingReferenceWidth.remove("refImageWidth");
        add("missing-reference-width", missingReferenceWidth);
        replaceField("zero-reference-height", "refImageHeight", 0);

        auto missingRois = legacyProfile();
        missingRois.remove("roiTemplates");
        add("missing-roi-array", missingRois);
        replaceField("wrong-roi-array-type", "roiTemplates", QJsonObject{});
        replaceField("non-object-roi", "roiTemplates", QJsonArray{42});
        replaceField("missing-roi-rectangle", "roiTemplates", QJsonArray{QJsonObject{}});

        const auto replaceRoiField = [&](const char* name, const QString& key, const QJsonValue& value) {
            auto document = legacyProfile();
            auto roi = document["roiTemplates"].toArray().first().toObject();
            roi[key] = value;
            document["roiTemplates"] = QJsonArray{roi};
            add(name, document);
        };
        replaceRoiField("wrong-rectangle-type", "relativeRect", QJsonArray{});
        replaceRoiField("incomplete-rectangle", "relativeRect",
            QJsonObject{{"x", 0.1}, {"y", 0.2}, {"width", 0.3}});
        replaceRoiField("non-numeric-rectangle", "relativeRect",
            QJsonObject{{"x", "0.1"}, {"y", 0.2}, {"width", 0.3}, {"height", 0.4}});
        replaceRoiField("zero-roi-width", "relativeRect",
            QJsonObject{{"x", 0.1}, {"y", 0.2}, {"width", 0}, {"height", 0.4}});
        replaceRoiField("negative-roi-height", "relativeRect",
            QJsonObject{{"x", 0.1}, {"y", 0.2}, {"width", 0.3}, {"height", -0.4}});
        replaceRoiField("wrong-detection-array-type", "detectionItems", QJsonObject{});
        replaceRoiField("wrong-pipeline-type", "pipelineConfig", QJsonArray{});
        replaceField("wrong-template-array-type", "templates", QJsonObject{});
        replaceField("non-object-template", "templates", QJsonArray{42});
        replaceField("missing-template-name", "templates", QJsonArray{QJsonObject{}});
        replaceField("empty-template-name", "templates",
            QJsonArray{QJsonObject{{"templateName", ""}}});
        replaceField("missing-template-file", "templates",
            QJsonArray{QJsonObject{{"templateName", "missing"}}});
    }

    void invalidProfilesPreserveLoadedDataAndCurrentRois()
    {
        QFETCH(QByteArray, contents);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto directory = root.filePath("inspection");
        QVERIFY(writeManifest(directory, contents));

        auto loaded = existingProfile();
        const auto original = loaded.toJson();
        const auto templatePixels = loaded.templates.first().templateImage.clone();
        const auto templatePolygon = loaded.templates.first().params.polygonPoints;
        QString error;
        QVERIFY(!loaded.loadFromDirectory(directory, &error));
        QVERIFY(!error.trimmed().isEmpty());
        QCOMPARE(loaded.toJson(), original);
        QCOMPARE(loaded.templates.first().templateImage.size(), templatePixels.size());
        QCOMPARE(loaded.templates.first().templateImage.type(), templatePixels.type());
        QCOMPARE(cv::norm(loaded.templates.first().templateImage, templatePixels, cv::NORM_INF), 0.0);
        QCOMPARE(loaded.templates.first().params.polygonPoints, templatePolygon);

        RoiManager rois;
        PipelineManager pipeline;
        rois.setFullImage(cv::Mat(80, 100, CV_8UC3, cv::Scalar(12)));
        RoiConfig roi("keep ROI", QRectF(5, 6, 20, 30));
        rois.addRoiConfig(roi);
        rois.setActiveRoi(roi.roiId);
        const auto originalRois = rois.exportAllConfigsToJson();
        QSignalSpy roiChanges(&rois, &RoiManager::roiConfigChanged);

        ProfileManager profiles(&rois, &pipeline, nullptr, root.path());
        QSignalSpy profileLoads(&profiles, &ProfileManager::profileLoaded);
        QVERIFY(profiles.getProfileList().isEmpty());
        QVERIFY(!profiles.loadProfile("profile-test"));
        QVERIFY(!profiles.loadProfile(""));
        QCOMPARE(rois.exportAllConfigsToJson(), originalRois);
        QCOMPARE(rois.getActiveRoiId(), roi.roiId);
        QCOMPARE(roiChanges.size(), 0);
        QCOMPARE(profileLoads.size(), 0);
    }

    void legacyOptionalFieldsAndDirectoryCaseRemainCompatible()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto directory = root.filePath("InSpEcTiOn");
        QVERIFY(writeManifest(directory, QJsonDocument(legacyProfile()).toJson()));

        InspectionProfile loaded;
        QString error;
        QVERIFY2(loaded.loadFromDirectory(directory, &error), qPrintable(error));
        QCOMPARE(loaded.profileId, QString("profile-test"));
        QCOMPARE(loaded.profileName, QString("InSpEcTiOn"));
        QCOMPARE(loaded.roiTemplates.size(), 1);
        QVERIFY(loaded.roiTemplates.first().detectionItems.isEmpty());
        QCOMPARE(loaded.roiTemplates.first().pipelineConfig.toJson(), PipelineConfig{}.toJson());
        QVERIFY(loaded.templates.isEmpty());
        QCOMPARE(loaded.globalPipelineConfig.toJson(), PipelineConfig{}.toJson());

        RoiManager rois;
        PipelineManager pipeline;
        rois.setFullImage(cv::Mat(80, 100, CV_8UC3, cv::Scalar(12)));
        RoiConfig roi("replace ROI", QRectF(5, 6, 20, 30));
        rois.addRoiConfig(roi);
        ProfileManager profiles(&rois, &pipeline, nullptr, root.path());
        QCOMPARE(profiles.getProfileList().size(), 1);
        QSignalSpy profileLoads(&profiles, &ProfileManager::profileLoaded);
        QVERIFY(profiles.loadProfile(loaded.profileId));
        QCOMPARE(profiles.activeProfileId(), loaded.profileId);
        QCOMPARE(profileLoads.size(), 1);
        QCOMPARE(rois.getRoiConfigs().size(), 1);
        const auto applied = rois.getRoiConfigs().first();
        QVERIFY(applied.roiId != roi.roiId);
        QCOMPARE(applied.roiName, QString("legacy ROI"));
        QCOMPARE(applied.roiRect, QRectF(10, 16, 30, 32));
    }

    void explicitlyEmptyRoiArrayRemainsAValidProfile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto document = legacyProfile();
        document["roiTemplates"] = QJsonArray{};
        QVERIFY(writeManifest(root.filePath("inspection"), QJsonDocument(document).toJson()));

        auto loaded = existingProfile();
        QString error;
        QVERIFY2(loaded.loadFromDirectory(root.filePath("inspection"), &error), qPrintable(error));
        QCOMPARE(loaded.profileId, QString("profile-test"));
        QVERIFY(loaded.roiTemplates.isEmpty());
        QVERIFY(loaded.templates.isEmpty());

        RoiManager rois;
        PipelineManager pipeline;
        rois.setFullImage(cv::Mat(80, 100, CV_8UC3, cv::Scalar(12)));
        RoiConfig roi("replace ROI", QRectF(5, 6, 20, 30));
        rois.addRoiConfig(roi);
        rois.setActiveRoi(roi.roiId);
        ProfileManager profiles(&rois, &pipeline, nullptr, root.path());
        QCOMPARE(profiles.getProfileList().size(), 1);
        QVERIFY(profiles.loadProfile(loaded.profileId));
        QVERIFY(rois.getRoiConfigs().isEmpty());
        QVERIFY(rois.getActiveRoiId().isEmpty());
    }
};

QTEST_MAIN(ProfileWorkflowTest)
#include "profile_workflow_test.moc"
