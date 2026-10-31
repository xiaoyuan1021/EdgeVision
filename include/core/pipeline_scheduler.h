#pragma once

#include <QObject>
#include <QTimer>
#include <QThreadPool>
#include <QQueue>
#include <QSet>
#include <QMutex>
#include <QFutureWatcher>
#include <QAtomicInt>
#include "pipeline_request.h"
#include "pipeline_result.h"
#include <functional>

class PipelineManager;

// Owner-thread scheduler. Workers receive immutable snapshots only.
// Reliable submissions reject overflow; preview submissions coalesce by caller.
class PipelineScheduler : public QObject
{
    Q_OBJECT

public:
    using Execute = std::function<PipelineContext(const cv::Mat&, const PipelineConfig&)>;
    explicit PipelineScheduler(Execute execute, QObject* parent = nullptr);
    enum class Policy { Reliable, LatestPreview };
    ~PipelineScheduler();

    // ========== 请求接口 ==========

    /**
     * 提交pipeline执行请求
     * @param image 输入图像
     * @param config Pipeline配置
     * @param priority 优先级（数值越大优先级越高）
     * @param caller 调用者标识（用于日志追踪）
     * @return 请求ID，可用于取消
     */
    qint64 submit(const cv::Mat& image, const PipelineConfig& config, int priority = 0, const QString& caller = {});

    /**
     * 提交pipeline执行请求（使用PipelineRequest对象）
     */
    qint64 submit(PipelineRequest request, Policy policy = Policy::Reliable);

    /**
     * 取消指定请求
     */
    void cancel(qint64 requestId);

    /**
     * 取消所有待处理请求
     */
    void cancelAll();

    // ========== 配置 ==========

    /**
     * 设置消抖延迟（毫秒）
     * 首次请求开始有界等待窗口，预览请求按 caller 合并
     * 默认：100ms
     */
    void setDebounceMs(int ms);

    /**
     * 设置最大队列长度
     * 可靠请求队列满时拒绝新请求，返回 -1 并发送 rejected
     * 默认：10
     */
    void setMaxQueueSize(int size);

    /**
     * 设置是否启用去重
     * 启用时，相同 ID 的请求不会重复执行
     * 默认：true
     */
    void setDeduplicationEnabled(bool enabled);

    // ========== 状态查询 ==========

    bool isProcessing() const { return m_processing.loadAcquire(); }
    int pendingCount() const;
    qint64 lastRequestId() const { return m_lastExecutedId.loadAcquire(); }

signals:
    /**
     * Pipeline执行完成
     */
    void finished(const PipelineResult& result);

    /**
     * 队列状态变化
     */
    void queueChanged(int pendingCount);

    /**
     * 执行状态变化
     */
    void processingChanged(bool processing);

    /**
     * 请求被取消
     */
    void cancelled(qint64 requestId);
    void rejected(qint64 requestId);

private slots:
    void onDebounceTimeout();
    void onTaskFinished();

private:
    void processNext();
    void executeRequest(PipelineRequest request);
    void emitQueueChanged();

    Execute m_execute;

    // 调度配置
    QTimer* m_debounceTimer;
    int m_debounceMs = 100;
    int m_maxQueueSize = 10;
    bool m_deduplicationEnabled = true;

    // 执行队列（按优先级排序）
    QQueue<PipelineRequest> m_queue;
    QSet<qint64> m_previewIds;
    mutable QMutex m_queueMutex;

    // 状态标志
    QAtomicInt m_processing{0};
    QAtomicInteger<qint64> m_lastExecutedId{-1};

    // 当前执行的任务
    QFutureWatcher<PipelineResult>* m_watcher = nullptr;
};
