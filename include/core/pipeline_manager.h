#pragma once

#include "pipeline.h"
#include "pipeline_steps.h"
#include "pipeline_scheduler.h"
#include "config/constants.h"
#include "core/i_pipeline_access.h"

#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <QAtomicInt>

class ImageProcessor;

/**
 * Pipeline管理器 - 负责Pipeline的创建、配置和执行
 *
 * 线程模型：
 * - execute() 在后台线程调用，使用局部 PipelineContext，不共享可变状态
 * - m_config 仅在UI线程读写，无需加锁
 * - Only accepted interactive results update m_lastContext; batch runs return values.
 */
class PipelineManager : public QObject, public IPipelineAccess
{
    Q_OBJECT

public:
    explicit PipelineManager(QObject* parent = nullptr);
    ~PipelineManager();

    // ========== IPipelineAccess 接口实现 ==========

    void resetConfigToDefaults() override;

    /// 获取配置的可变引用（UI线程直接读写，替代大量 trivial setter）
    [[deprecated("使用 updateConfig() 替代")]]
    PipelineConfig& mutableConfig() { return m_config; }

    /// 获取配置的常量引用
    const PipelineConfig& config() const override { return m_config; }

    // 获取当前配置的拷贝（用于并发快照）
    PipelineConfig getConfigSnapshot() const override { return m_config; }

    // 设置配置（仅UI线程调用）
    void setConfig(const PipelineConfig& config) override {
        m_config = config;
    }

    /// 批量修改配置（推荐方式）
    void updateConfig(std::function<void(PipelineConfig&)> updater) override {
        updater(m_config);
    }

    // ========== 步骤控制 ==========

    /// 启用/禁用指定步骤（调用后需 rebuildPipeline 生效）
    void setStepEnabled(int stepIndex, bool enabled) override;

    /// 查询指定步骤是否启用
    bool isStepEnabled(int stepIndex) const override;

    /// 根据当前 m_config.stepEnabled/stepOrder 重建 pipeline
    void rebuildPipeline() override;

    // ========== 算法队列管理 ==========

    void addAlgorithmStep(const AlgorithmStep& step) override;
    void removeAlgorithmStep(int index) override;
    void swapAlgorithmStep(int index1,int index2) override;
    void clearAlgorithmQueue();
    const QVector<AlgorithmStep>& getAlgorithmQueue() const override { return m_config.algorithmQueue; }

    // ========== Pipeline执行 ==========

    // Execute a task snapshot without changing manager/UI state.
    // 输入：BGR图像 + 配置（步骤通过 ctx.config 读取）
    // 返回：处理结果上下文
    PipelineContext execute(const cv::Mat& inputImage, const PipelineConfig& config);

    // 获取最后一次执行的上下文（线程安全，返回拷贝）
    PipelineContext getLastContext() const override {
        QMutexLocker locker(&m_contextMutex);
        return m_lastContext;
    }

    double lastExecMs() const { QMutexLocker lock(&m_contextMutex); return m_lastExecMs; }

    // UI thread: publish only a result accepted by the display controller.
    void acceptResult(const PipelineResult& result);
    quint64 displayGeneration() const { return m_displayGeneration; }
    void expectRequest(qint64 id) { m_expectedRequestId = id; }
    bool isCurrentResult(const PipelineResult& result, bool streaming = false) const;

    void updateAlgorithmStep(int index, const AlgorithmStep& step) override;

    void setDisplayMode(DisplayConfig::Mode mode) override;
    DisplayConfig::Mode getDisplayMode() const { return m_displayMode; }

    // 快速重新渲染上次Pipeline结果
    cv::Mat getLastDisplayWithMode(DisplayConfig::Mode mode) const;

    bool hasLastResult() const;

    /// 清除上次Pipeline结果（图片切换时调用，防止旧结果污染新图片显示）
    void clearLastResult();

    // ========== per-ROI缓存 ===========

    /// 将Pipeline结果存入指定ROI的缓存
    void cacheResult(const QString& roiId, const PipelineContext& ctx);

    /// 获取指定ROI的缓存结果（无缓存返回空ctx）
    PipelineContext getCachedResult(const QString& roiId) const;

    /// 检查指定ROI是否有缓存
    bool hasCachedResult(const QString& roiId) const;

    /// 获取指定ROI的缓存渲染图像
    cv::Mat getCachedDisplay(const QString& roiId, DisplayConfig::Mode mode) const;

    /// 清除指定ROI的缓存
    void clearCachedResult(const QString& roiId);

    /// 清除所有ROI缓存
    void clearAllCachedResults();

    // ========== 调度器接口 ==========

    /// 获取调度器（用于异步执行）
    PipelineScheduler* scheduler() { return m_scheduler.get(); }
    const PipelineScheduler* scheduler() const { return m_scheduler.get(); }

    /// 异步执行Pipeline（通过调度器）
    qint64 executeAsync(const cv::Mat& image, const PipelineConfig& config, int priority = 0, const QString& caller = {});

    // ========== 颜色过滤控制 ==========

    void resetPipeline();

signals:
    void pipelineFinished(const QString& message);
    void algorithmQueueChanged(int count);
    void pipelineReset();

    /// 异步执行完成信号
    void asyncFinished(const PipelineResult& result);

private:
    // Protect accepted display results and timing.
    mutable QMutex m_contextMutex;

    // 配置（仅在UI线程读写，不需要锁保护）
    PipelineConfig m_config;

    quint64 m_displayGeneration = 0;
    qint64 m_expectedRequestId = -1;

    // 最后执行结果
    PipelineContext m_lastContext;

    // per-ROI缓存：roiId -> PipelineContext
    QHash<QString, PipelineContext> m_roiCache;
    mutable QMutex m_roiCacheMutex;

    // 基准性能数据
    double m_lastExecMs = 0;

    DisplayConfig::Mode m_displayMode = DisplayConfig::Mode::MaskGreenWhite;
    float m_overlayAlpha = AppConstants::DEFAULT_OVERLAY_ALPHA;

    // 调度器
    std::unique_ptr<PipelineScheduler> m_scheduler;
};
