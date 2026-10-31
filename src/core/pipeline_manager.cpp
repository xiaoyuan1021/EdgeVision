#include "pipeline_manager.h"
#include "pipeline_executor.h"
#include "config/constants.h"
#include "logger.h"
#include "algorithm/display_renderer.h"

PipelineManager::PipelineManager(QObject* parent)
    : QObject(parent)
    , m_scheduler(std::make_unique<PipelineScheduler>(&PipelineExecutor::execute))
{
    resetConfigToDefaults();

    // 连接调度器信号
    connect(m_scheduler.get(), &PipelineScheduler::finished,
            this, &PipelineManager::asyncFinished);
}

PipelineManager::~PipelineManager() = default;

// ========== 配置管理 ==========

void PipelineManager::resetConfigToDefaults()
{
    m_config.resetToDefaults();
    spdlog::info("[PipelineManager] resetConfigToDefaults");
}

// ========== 算法队列管理 ==========

void PipelineManager::addAlgorithmStep(const AlgorithmStep& step)
{
    m_config.algorithmQueue.append(step);
    emit algorithmQueueChanged(m_config.algorithmQueue.size());
}

void PipelineManager::removeAlgorithmStep(int index)
{
    if (index >= 0 && index < m_config.algorithmQueue.size())
    {
        m_config.algorithmQueue.removeAt(index);
        emit algorithmQueueChanged(m_config.algorithmQueue.size());
    }
}

void PipelineManager::swapAlgorithmStep(int index1, int index2)
{
    if (index1 >= 0 && index2 >= 0 && index1 < m_config.algorithmQueue.size() && index2 < m_config.algorithmQueue.size())
    {
        m_config.algorithmQueue.swapItemsAt(index1, index2);
        emit algorithmQueueChanged(m_config.algorithmQueue.size());
    }
}

void PipelineManager::clearAlgorithmQueue()
{
    m_config.algorithmQueue.clear();
    emit algorithmQueueChanged(0);
}

// ========== Pipeline执行 ==========

PipelineContext PipelineManager::execute(const cv::Mat& inputImage, const PipelineConfig& config)
{
    return PipelineExecutor::execute(inputImage, config);
}

bool PipelineManager::isCurrentResult(const PipelineResult& result, bool streaming) const
{
    return m_expectedRequestId >= 0
        && (streaming ? result.requestId() <= m_expectedRequestId : result.requestId() == m_expectedRequestId)
        && result.request().identity().generation == m_displayGeneration
        && result.request().config().toJson() == m_config.toJson();
}

void PipelineManager::acceptResult(const PipelineResult& result)
{
    if (!result.isSuccess()) return;
    {
        QMutexLocker locker(&m_contextMutex);
        m_lastContext = result.context();
        m_lastExecMs = result.elapsedMs();
    }
    emit pipelineFinished(result.context().reason.isEmpty()
        ? QStringLiteral("Pipeline执行完成") : result.context().reason);
}

// ========== 调度器接口 ==========

qint64 PipelineManager::executeAsync(const cv::Mat& image, const PipelineConfig& config, int priority, const QString& caller)
{
    return m_scheduler->submit(image, config, priority, caller);
}

// ========== 非trivial方法 ==========

void PipelineManager::updateAlgorithmStep(int index, const AlgorithmStep& step)
{
    if (index >= 0 && index < m_config.algorithmQueue.size())
    {
        m_config.algorithmQueue[index] = step;
        emit algorithmQueueChanged(m_config.algorithmQueue.size());
    }
}

void PipelineManager::clearLastResult()
{
    ++m_displayGeneration;
    m_expectedRequestId = -1;
    m_scheduler->cancelAll();
    clearAllCachedResults();
    QMutexLocker locker(&m_contextMutex);
    m_lastContext = PipelineContext();
    m_lastExecMs = 0;
}

void PipelineManager::setDisplayMode(DisplayConfig::Mode mode)
{
    m_displayMode = mode;
}

cv::Mat PipelineManager::getLastDisplayWithMode(DisplayConfig::Mode mode) const
{
    QMutexLocker locker(&m_contextMutex);
    if (m_lastContext.srcBgr.empty()) return cv::Mat();

    return DisplayRenderer::render(m_lastContext, mode);
}

bool PipelineManager::hasLastResult() const
{
    QMutexLocker locker(&m_contextMutex);
    return !m_lastContext.srcBgr.empty();
}

void PipelineManager::resetPipeline()
{
    clearLastResult();
    m_config.algorithmQueue.clear();
    m_config.shapeFilter.clear();
    // [FIX] 重置步骤配置到默认值
    m_config.stepEnabled = {false, false, false, false, false, false, false, false, false};
    m_config.stepOrder = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    m_config.enableObjectDetection = false;
    m_config.objectDetectionApplyEnabled = false;
    m_displayMode = DisplayConfig::Mode::MaskGreenWhite;
    m_overlayAlpha = AppConstants::DEFAULT_OVERLAY_ALPHA;

    emit pipelineReset();
    emit algorithmQueueChanged(0);
}

void PipelineManager::setStepEnabled(int stepIndex, bool enabled)
{
    if (stepIndex < 0 || stepIndex >= PipelineConfig::STEP_COUNT) return;
    m_config.stepEnabled[stepIndex] = enabled;
}

bool PipelineManager::isStepEnabled(int stepIndex) const
{
    if (stepIndex < 0 || stepIndex >= PipelineConfig::STEP_COUNT) return false;
    return m_config.stepEnabled[stepIndex];
}

void PipelineManager::rebuildPipeline()
{
    // Step selection/order is read from each submitted configuration snapshot.
    emit pipelineReset();
}

// ========== per-ROI缓存 ==========

void PipelineManager::cacheResult(const QString& roiId, const PipelineContext& ctx)
{
    if (roiId.isEmpty()) return;
    QMutexLocker locker(&m_roiCacheMutex);
    m_roiCache[roiId] = ctx;
}

PipelineContext PipelineManager::getCachedResult(const QString& roiId) const
{
    QMutexLocker locker(&m_roiCacheMutex);
    return m_roiCache.value(roiId, PipelineContext());
}

bool PipelineManager::hasCachedResult(const QString& roiId) const
{
    QMutexLocker locker(&m_roiCacheMutex);
    return m_roiCache.contains(roiId) && !m_roiCache[roiId].srcBgr.empty();
}

cv::Mat PipelineManager::getCachedDisplay(const QString& roiId, DisplayConfig::Mode mode) const
{
    QMutexLocker locker(&m_roiCacheMutex);
    auto it = m_roiCache.find(roiId);
    if (it == m_roiCache.end() || it->srcBgr.empty()) return cv::Mat();
    return DisplayRenderer::render(*it, mode);
}

void PipelineManager::clearCachedResult(const QString& roiId)
{
    QMutexLocker locker(&m_roiCacheMutex);
    m_roiCache.remove(roiId);
}

void PipelineManager::clearAllCachedResults()
{
    QMutexLocker locker(&m_roiCacheMutex);
    m_roiCache.clear();
}

