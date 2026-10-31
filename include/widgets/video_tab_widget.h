#ifndef VIDEO_TAB_WIDGET_H
#define VIDEO_TAB_WIDGET_H

#include <QWidget>
#include <QString>
#include <QVector>
#include "video_manager.h"
#include "widgets/i_tab_interfaces.h"

namespace Ui {
class VideoTabWidget;
}

class VideoTabWidget : public QWidget, public ISignalConnectable
{
    Q_OBJECT

public:
    explicit VideoTabWidget(QWidget* parent = nullptr);
    ~VideoTabWidget();

    // 获取视频管理器实例
    VideoManager* getVideoManager() const { return m_videoManager; }

    void connectSignals(const SignalContext& ctx,
                        std::function<void()> onExecutePipeline,
                        std::function<void()> onConfigSaved = nullptr) override;

signals:
    void videoFrameReady(const cv::Mat& frame);
    void videoSourceChanged(const QString& source);

private slots:
    void onOpenFileClicked();
    void onOpenCameraClicked();
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onProgressChanged(int value);

    // 视频管理器信号槽
    void onFrameReady(const cv::Mat& frame);
    void onPlaybackStateChanged(VideoManager::PlaybackState state);
    void onVideoOpened(const QString& source);
    void onVideoClosed();
    void onErrorOccurred(const QString& error);

private:
    void updateUIState();
    void updateCameraList();
    void updateProgress();

    Ui::VideoTabWidget* m_ui;
    VideoManager* m_videoManager;
    QVector<QString> m_cameraList;
    bool m_isUpdatingProgress;
};

#endif // VIDEO_TAB_WIDGET_H