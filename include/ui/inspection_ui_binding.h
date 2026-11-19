#pragma once

#include <QObject>
#include <QPointer>

class AutoDetectionController;
class InspectionResultsDialog;
class RoiManager;
class QLabel;
class QPushButton;
class QStatusBar;
class QWidget;

class InspectionUiBinding : public QObject
{
public:
    InspectionUiBinding(AutoDetectionController* detection, RoiManager* rois,
        QPushButton* start, QPushButton* stop, QPushButton* results,
        QStatusBar* status, QLabel* label, QWidget* window);

private:
    void showResults();
    void refreshResults();
    void refreshRetryAvailability();
    void activateImage(const QString& imageId);

    QPointer<AutoDetectionController> m_detection;
    QPointer<RoiManager> m_rois;
    QPointer<QPushButton> m_start;
    QPointer<QPushButton> m_stop;
    QPointer<QStatusBar> m_status;
    QPointer<QLabel> m_label;
    QPointer<QWidget> m_window;
    QPointer<InspectionResultsDialog> m_dialog;
};
