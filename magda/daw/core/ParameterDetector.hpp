#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

#include "ParameterInfo.hpp"

namespace magda {

struct DetectedParameterInfo {
    int paramIndex = -1;
    juce::String unit;
    ParameterScale scale = ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool bipolar = false;
    bool modulatable = true;
    std::vector<juce::String> choices;
    float confidence = 0.0f;  // 0 = needs AI, 1 = resolved deterministically
};

struct ParameterScanInput {
    int paramIndex = -1;
    juce::String name;
    juce::String label;  // Plugin-reported unit/label (often empty)
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    int stateCount = 0;                      // Number of discrete states (0 = continuous)
    std::vector<juce::String> displayTexts;  // Sampled display text at normalized points
};

namespace ParameterDetector {

/** Run deterministic heuristic detection on a set of parameters.
    Returns one DetectedParameterInfo per input. */
std::vector<DetectedParameterInfo> detect(const std::vector<ParameterScanInput>& params);

/** Run AI detection on unresolved parameters (confidence == 0).
    onProgress reports (resolved, total) on the message thread as params are parsed.
    onComplete delivers final results on the message thread.
    Set cancelFlag to true to abort the request. */
void detectWithAI(const juce::String& pluginName, const std::vector<ParameterScanInput>& params,
                  const std::vector<DetectedParameterInfo>& deterministicResults,
                  float confidenceThreshold, std::shared_ptr<std::atomic<bool>> cancelFlag,
                  std::function<void(int resolved, int total)> onProgress,
                  std::function<void(std::vector<DetectedParameterInfo>)> onComplete);

}  // namespace ParameterDetector

}  // namespace magda
