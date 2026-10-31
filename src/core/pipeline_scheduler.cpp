#include "pipeline_scheduler.h"

#include "logger.h"
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

PipelineScheduler::PipelineScheduler(Execute execute, QObject* parent)
    : QObject(parent)
    , m_execute(std::move(execute))
    , m_debounceTimer(new QTimer(this))
{
    // 配置消抖定时器
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(m_debounceMs);
    connect(m_debounceTimer, &QTimer::timeout, this, &PipelineScheduler::onDebounceTimeout);

    // 创建 FutureWatcher
    m_watcher = new QFutureWatcher<PipelineResult>(this);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::finished, this, &PipelineScheduler::onTaskFinished);
}

PipelineScheduler::~PipelineScheduler()
{
    shutdown();
}

void PipelineScheduler::shutdown()
{
    if (m_stopped) return;
    m_stopped = true;
    m_debounceTimer->stop();
    m_watcher->disconnect(this);
    cancelAll();
    if (m_watcher && m_watcher->isRunning()) {
        m_watcher->waitForFinished();
    }
    m_processing.storeRelease(0);
}

// ========== 请求接口 ==========

qint64 PipelineScheduler::submit(const cv::Mat& image, const PipelineConfig& config, int priority, const QString& caller)
{
    PipelineRequest request(image, config, priority, caller);
    return submit(std::move(request));
}

qint64 PipelineScheduler::submit(PipelineRequest request, Policy policy)
{
    qint64 requestId = request.id();
    if (m_stopped) {
        emit rejected(requestId);
        return -1;
    }

    QMutexLocker locker(&m_queueMutex);

    // 去重检查：如果队列中已有相同请求，忽略
    if (m_deduplicationEnabled) {
        for (const auto& existing : m_queue) {
            if (existing.id() == requestId) {
                spdlog::debug("[PipelineScheduler] 忽略重复请求: {}", requestId);
                return requestId;
            }
        }
    }

    QList<qint64> superseded;
    if (policy == Policy::LatestPreview) {
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (m_previewIds.contains(m_queue[i].id()) && m_queue[i].caller() == request.caller()) {
                superseded.append(m_queue[i].id());
                m_previewIds.remove(m_queue[i].id());
                m_queue.removeAt(i);
            }
        }
    }
    if (m_queue.size() >= m_maxQueueSize) {
        locker.unlock();
        emit rejected(requestId);
        return -1;
    }
    if (policy == Policy::LatestPreview) m_previewIds.insert(requestId);
    m_queue.enqueue(std::move(request));
    locker.unlock();
    for (qint64 id : superseded) emit cancelled(id);
    emitQueueChanged();
    // Do not restart: continuous frame submission must not starve execution.
    if (!m_processing.loadAcquire() && !m_debounceTimer->isActive()) {
        m_debounceTimer->start();
    }

    return requestId;
}

void PipelineScheduler::cancel(qint64 requestId)
{
    QMutexLocker locker(&m_queueMutex);

    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id() == requestId) {
            spdlog::debug("[PipelineScheduler] 取消请求: {}", requestId);
            m_queue.removeAt(i);
            m_previewIds.remove(requestId);
            locker.unlock();
            emit cancelled(requestId);
            emitQueueChanged();
            return;
        }
    }
}

void PipelineScheduler::cancelAll()
{
    QMutexLocker locker(&m_queueMutex);

    if (m_queue.isEmpty()) return;

    spdlog::debug("[PipelineScheduler] 取消所有请求，队列长度: {}", m_queue.size());
    QList<qint64> ids;
    for (const auto& request : m_queue) ids.append(request.id());
    m_queue.clear();
    m_previewIds.clear();
    locker.unlock();
    for (qint64 id : ids) emit cancelled(id);

    m_debounceTimer->stop();
    emitQueueChanged();
}

// ========== 配置 ==========

void PipelineScheduler::setDebounceMs(int ms)
{
    m_debounceMs = std::max(0, ms);
    m_debounceTimer->setInterval(m_debounceMs);
}

void PipelineScheduler::setMaxQueueSize(int size)
{
    m_maxQueueSize = std::max(1, size);
}

void PipelineScheduler::setDeduplicationEnabled(bool enabled)
{
    m_deduplicationEnabled = enabled;
}

// ========== 状态查询 ==========

int PipelineScheduler::pendingCount() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_queue.size();
}

// ========== 内部实现 ==========

void PipelineScheduler::onDebounceTimeout()
{
    processNext();
}

void PipelineScheduler::processNext()
{
    // 如果正在执行，等待完成
    if (m_processing.loadAcquire()) {
        spdlog::info("[PipelineScheduler] 正在执行，等待完成");
        return;
    }

    QMutexLocker locker(&m_queueMutex);

    // 队列为空
    if (m_queue.isEmpty()) {
        return;
    }

    // 取出最高优先级的请求
    int highestIdx = 0;
    for (int i = 1; i < m_queue.size(); ++i) {
        if (m_queue[i].priority() > m_queue[highestIdx].priority()) {
            highestIdx = i;
        }
    }

    PipelineRequest request = std::move(m_queue[highestIdx]);
    m_queue.removeAt(highestIdx);
    m_previewIds.remove(request.id());

    locker.unlock();
    emitQueueChanged();

    // 执行请求
    executeRequest(std::move(request));
}

void PipelineScheduler::executeRequest(PipelineRequest request)
{
    m_processing.storeRelease(1);
    emit processingChanged(true);

    // 记录调用者
    if (!request.caller().isEmpty()) {
        spdlog::debug("[PipelineScheduler] 执行请求: {} (来自: {})", request.id(), request.caller().toStdString());
    }

    QFuture<PipelineResult> future = QtConcurrent::run(
        [execute = m_execute, req = std::move(request)]() mutable -> PipelineResult {
            QElapsedTimer timer;
            timer.start();

            try {
                PipelineContext ctx = execute(req.image(), req.config());
                double elapsed = timer.elapsed();

                if (!ctx.pass) return PipelineResult::failure(std::move(req), ctx.reason, elapsed);
                return PipelineResult::success(std::move(req), std::move(ctx), elapsed);
            } catch (const cv::Exception& ex) {
                double elapsed = timer.elapsed();
                QString error = QString("OpenCV 错误: %1").arg(ex.what());
                spdlog::error("{}", error.toStdString());
                return PipelineResult::failure(req, error, elapsed);
            } catch (const std::exception& ex) {
                double elapsed = timer.elapsed();
                QString error = QString("异常: %1").arg(ex.what());
                spdlog::error("{}", error.toStdString());
                return PipelineResult::failure(req, error, elapsed);
            } catch (...) {
                double elapsed = timer.elapsed();
                QString error = "未知异常";
                spdlog::error("{}", error.toStdString());
                return PipelineResult::failure(req, error, elapsed);
            }
        }
    );

    m_watcher->setFuture(future);
}

void PipelineScheduler::onTaskFinished()
{
    if (!m_watcher) return;

    PipelineResult result = m_watcher->result();

    m_lastExecutedId.storeRelease(result.requestId());
    m_processing.storeRelease(0);
    emit processingChanged(false);

    // 只在失败或耗时 >0ms 时打印
    if (!result.isSuccess() || result.elapsedMs() > 0) {
        spdlog::debug("[PipelineScheduler] 请求执行完成: {} 耗时: {}ms 成功: {}",
            result.requestId(), result.elapsedMs(), result.isSuccess());
    }

    emit finished(result);

    // 检查队列中是否还有待处理的请求
    if (pendingCount() > 0) {
        // 延迟处理下一个请求，避免过于频繁
        QTimer::singleShot(10, this, &PipelineScheduler::processNext);
    }
}

void PipelineScheduler::emitQueueChanged()
{
    emit queueChanged(pendingCount());
}
