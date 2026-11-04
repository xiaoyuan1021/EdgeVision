#pragma once
#include <QObject>

class QTabWidget;
class QWidget;
class QTreeWidget;
class TabManager;
class PipelineManager;
class RoiManager;
class ImageView;
class RoiUiController;
class DetectionUiController;
class ConfigController;
class ProfileManager;
class DisplayModeManager;
class PipelineResultHandler;

// Owns lazy tab wiring and detection selection/visibility, independently of
// the window shell. Dependencies are borrowed and outlive this coordinator.
class DetectionWorkspace : public QObject {
    Q_OBJECT
public:
    DetectionWorkspace(QTabWidget* tabs, TabManager* tabManager,
        PipelineManager* pipeline, RoiManager& rois, ImageView* view,
        RoiUiController* roiUi, DetectionUiController* detectionUi,
        ConfigController* config, ProfileManager* profiles,
        DisplayModeManager* display, PipelineResultHandler* results,
        QObject* parent = nullptr);
    void ensureTabExists(const QString& name);
    void restoreTabVisibility();
    void openPipelineConfig();
    void addDetection();
    void deleteDetection(QTreeWidget* tree);
    void stopVideo();
signals:
    void refreshRequested();
private:
    void onTabCreated(const QString& name, QWidget* widget);
    void selectDetection(const QString& roiId, const QString& detectionId);
    QTabWidget* m_tabs;
    TabManager* m_tabManager;
    PipelineManager* m_pipelineManager;
    RoiManager& m_roiManager;
    ImageView* m_view;
    RoiUiController* m_roiUiController;
    DetectionUiController* m_detectionUiController;
    ConfigController* m_configController;
    ProfileManager* m_profileManager;
    DisplayModeManager* m_displayModeManager;
    PipelineResultHandler* m_pipelineResultHandler;
};
