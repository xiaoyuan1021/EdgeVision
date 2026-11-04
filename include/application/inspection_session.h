#pragma once

#include <QObject>
#include <opencv2/core/mat.hpp>

class PipelineManager;
class RoiManager;
class RoiUiController;
class TabManager;
class ImageView;
class DisplayModeManager;

// Coordinates image identity, configuration and preview requests. No window or
// Designer UI dependency; the window only presents the emitted status.
class InspectionSession : public QObject {
    Q_OBJECT
public:
    InspectionSession(PipelineManager* pipeline, RoiManager* rois,
        RoiUiController* roiUi, TabManager* tabs, ImageView* view,
        DisplayModeManager* display, QObject* parent = nullptr);
    void loadImage(const cv::Mat& image, const QString& path);
    void requestRefresh();
    void shutdown();
signals:
    void imageSessionChanged();
    void statusMessage(const QString& message, int timeout = 0);
private:
    void showImage(const cv::Mat& image);
    void applyImageConfig(const QString& imageId);
    void currentImageChanged(const QString& imageId);
    PipelineManager* m_pipeline;
    RoiManager* m_rois;
    RoiUiController* m_roiUi;
    TabManager* m_tabs;
    ImageView* m_view;
    DisplayModeManager* m_display;
    bool m_stopped = false;
};
