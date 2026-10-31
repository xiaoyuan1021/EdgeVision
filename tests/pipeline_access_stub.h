#pragma once
#include "core/i_pipeline_access.h"
class PipelineStub : public IPipelineAccess {
public:
    PipelineConfig value;
    int writes = 0;
    PipelineConfig getConfigSnapshot() const override { return value; }
    void setConfig(const PipelineConfig& config) override { value = config; ++writes; }
    void updateConfig(std::function<void(PipelineConfig&)> update) override { update(value); ++writes; }
    const PipelineConfig& config() const override { return value; }
    void resetConfigToDefaults() override { value.resetToDefaults(); }
    bool isStepEnabled(int i) const override { return value.stepEnabled.at(i); }
    void setStepEnabled(int i, bool enabled) override { value.stepEnabled.at(i) = enabled; }
    void rebuildPipeline() override {}
    void addAlgorithmStep(const AlgorithmStep& step) override { value.algorithmQueue.append(step); }
    void removeAlgorithmStep(int i) override { value.algorithmQueue.removeAt(i); }
    void swapAlgorithmStep(int a, int b) override { value.algorithmQueue.swapItemsAt(a, b); }
    const QVector<AlgorithmStep>& getAlgorithmQueue() const override { return value.algorithmQueue; }
    void updateAlgorithmStep(int i, const AlgorithmStep& step) override { value.algorithmQueue[i] = step; }
    void setDisplayMode(DisplayConfig::Mode) override {}
    PipelineContext getLastContext() const override { return {}; }
};
