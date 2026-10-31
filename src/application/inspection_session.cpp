#include "application/inspection_session.h"
#include "core/pipeline_manager.h"
#include "core/roi_manager.h"
#include "controllers/roi_ui_controller.h"
#include "widgets/tab_manager.h"
#include "ui/image_view.h"
#include "ui/display_mode_manager.h"
#include "algorithm/image_utils.h"

InspectionSession::InspectionSession(PipelineManager* pipeline, RoiManager* rois,
    RoiUiController* roiUi, TabManager* tabs, ImageView* view,
    DisplayModeManager* display, QObject* parent)
    : QObject(parent), m_pipeline(pipeline), m_rois(rois), m_roiUi(roiUi),
      m_tabs(tabs), m_view(view), m_display(display)
{
    connect(rois, &RoiManager::inputChanged, pipeline, &PipelineManager::clearLastResult);
    connect(rois, &RoiManager::imageSwitching, this, [this](const QString& from, const QString&) {
        if (!from.isEmpty()) m_rois->saveImagePipelineConfig(from, m_pipeline->getConfigSnapshot());
        m_roiUi->saveCurrentRoiPipelineConfig();
    });
    connect(rois, &RoiManager::currentImageChanged, this, &InspectionSession::currentImageChanged);
}

void InspectionSession::currentImageChanged(const QString& imageId)
{
    if (m_stopped) return;
    m_pipeline->scheduler()->cancelAll();
    m_pipeline->clearLastResult();
    m_tabs->clearAllResults();
    m_view->clear();
    m_view->setZoomEnabled(true);
    applyImageConfig(imageId);
    showImage(m_rois->getCurrentImage());
    m_roiUi->syncRoiConfigsToWidget();
    emit imageSessionChanged();
}

void InspectionSession::loadImage(const cv::Mat& image, const QString& path)
{
    if (m_stopped || image.empty()) return;
    const bool replacing = !m_rois->getCurrentImageId().isEmpty();
    if (replacing) {
        m_rois->saveImagePipelineConfig(m_rois->getCurrentImageId(), m_pipeline->getConfigSnapshot());
        m_roiUi->saveCurrentRoiPipelineConfig();
    }
    m_rois->setFullImage(image, path);
    // New images emit currentImageChanged; replacement keeps the same identity.
    if (replacing) currentImageChanged(m_rois->getCurrentImageId());
}

void InspectionSession::showImage(const cv::Mat& image)
{
    if (m_stopped) return;
    if (image.empty()) {
        m_view->clear();
        m_pipeline->clearLastResult();
        m_tabs->clearAllResults();
    } else {
        m_view->setImage(ImageUtils::matToQImage(image));
    }
}

void InspectionSession::applyImageConfig(const QString& imageId)
{
    m_pipeline->setConfig(imageId.isEmpty() ? PipelineConfig{} : m_rois->loadImagePipelineConfig(imageId));
    m_pipeline->rebuildPipeline();
}

void InspectionSession::requestRefresh()
{
    if (m_stopped) return;
    const cv::Mat image = m_rois->getCurrentImage();
    if (image.empty()) {
        if (m_rois->isRoiActive()) emit statusMessage(QStringLiteral("ROI 不在图像有效范围内"), 3000);
        return;
    }
    m_display->applyModeForCurrentTab();
    PipelineIdentity identity{m_rois->getCurrentImageId(), m_rois->getActiveRoiId(),
                              m_pipeline->displayGeneration()};
    PipelineRequest request(image, m_pipeline->getConfigSnapshot(), 0, "InspectionSession", identity);
    const auto id = m_pipeline->scheduler()->submit(std::move(request), PipelineScheduler::Policy::LatestPreview);
    if (id >= 0) {
        m_pipeline->expectRequest(id);
        emit statusMessage(QStringLiteral("正在处理..."));
    } else {
        emit statusMessage(QStringLiteral("处理队列已满，请稍后重试"), 3000);
    }
}

void InspectionSession::shutdown()
{
    if (m_stopped) return;
    m_stopped = true;
    m_pipeline->clearLastResult();
    m_pipeline->scheduler()->shutdown();
}
