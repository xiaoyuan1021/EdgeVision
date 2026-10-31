#include "pipeline.h"
#include "logger.h"

// ========== Pipeline类实现 ==========

Pipeline::Pipeline() = default;

// ========== 步骤管理 ==========

void Pipeline::add(std::unique_ptr<IPipelineStep> step)
{
    if (step) {
        steps_.push_back(std::move(step));
    } else {
        spdlog::info("[Pipeline] 警告：尝试添加空步骤");
    }
}

bool Pipeline::remove(int index)
{
    if (index < 0 || index >= static_cast<int>(steps_.size())) {
        spdlog::debug("[Pipeline] 警告：尝试移除无效索引 {}", index);
        return false;
    }
    
    steps_.erase(steps_.begin() + index);
    spdlog::debug("[Pipeline] 已移除步骤 {}", index);
    return true;
}

void Pipeline::clear()
{
    steps_.clear();
    spdlog::info("[Pipeline] 已清空所有步骤");
}

size_t Pipeline::size() const
{
    return steps_.size();
}

bool Pipeline::empty() const
{
    return steps_.empty();
}

bool Pipeline::swap(int index1, int index2)
{
    if (index1 < 0 || index1 >= static_cast<int>(steps_.size()) ||
        index2 < 0 || index2 >= static_cast<int>(steps_.size())) {
        spdlog::debug("[Pipeline] 警告：尝试交换无效索引 {} 和 {}", index1, index2);
        return false;
    }
    
    std::swap(steps_[index1], steps_[index2]);
    spdlog::debug("[Pipeline] 已交换步骤 {} 和 {}", index1, index2);
    return true;
}

const IPipelineStep* Pipeline::getStep(int index) const
{
    if (index < 0 || index >= static_cast<int>(steps_.size())) {
        return nullptr;
    }
    return steps_[index].get();
}

IPipelineStep* Pipeline::getStep(int index)
{
    if (index < 0 || index >= static_cast<int>(steps_.size())) {
        return nullptr;
    }
    return steps_[index].get();
}

// ========== 执行 ==========

void Pipeline::run(PipelineContext& ctx)
{
    const auto runStep = [&ctx](IPipelineStep& step) {
        try {
            step.run(ctx);
        } catch (const std::exception& ex) {
            ctx.pass = false;
            ctx.reason = QString("Pipeline step failed: %1").arg(ex.what());
        } catch (...) {
            ctx.pass = false;
            ctx.reason = "Pipeline step failed: unknown exception";
        }
    };
    if (steps_.empty()) {
        spdlog::info("[Pipeline] 警告：没有步骤可执行");
        return;
    }

    if (!ctx.config) {
        for (auto& step : steps_) {
            runStep(*step);
            if (!ctx.pass) break;
        }
        return;
    }
    // Resolve by type, not container position: order belongs to this task.
    std::array<bool, PipelineConfig::STEP_COUNT> visited{};
    for (int type : ctx.config->stepOrder) {
        if (type < 0 || type >= PipelineConfig::STEP_COUNT || visited[type]) continue;
        visited[type] = true;
        if (!ctx.config->stepEnabled[type]) continue;
        for (auto& step : steps_) {
            if (static_cast<int>(step->stepType()) == type) {
                runStep(*step);
                break;
            }
        }
        if (!ctx.pass) break;
    }
}
