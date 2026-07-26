#pragma once

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <vector>

namespace magda::agent {

class CancellationToken;

/**
 * Advisory output from a low-latency local model.
 *
 * The policy predicts Remote API operation IDs, not a second command taxonomy.
 * Arguments and extracted slots are untrusted until the normal tool schema and
 * execution boundary validate them.
 */
enum class FastInferenceDecision {
    DirectPlan,
    NarrowAgentTools,
    NeedsGeneralAgent,
    Ambiguous,
    Unsupported
};

struct OperationCandidate {
    juce::String operationId;
    juce::var arguments;
    float confidence = 0.0f;
};

struct ExtractedSlot {
    juce::String name;
    juce::var value;
    float confidence = 0.0f;
};

struct ContextRequirement {
    juce::String key;
    float relevance = 0.0f;
    bool required = false;
};

struct FastInferenceResult {
    FastInferenceDecision decision = FastInferenceDecision::NeedsGeneralAgent;
    float confidence = 0.0f;
    std::vector<OperationCandidate> operations;
    std::vector<ExtractedSlot> slots;
    std::vector<ContextRequirement> requiredContext;
};

struct FastInferenceRequest {
    juce::String userMessage;
    juce::String agentId;
    juce::var contextFeatures;
    std::vector<juce::String> availableOperations;
};

class FastInferencePolicy {
  public:
    virtual ~FastInferencePolicy() = default;
    virtual FastInferenceResult evaluate(const FastInferenceRequest& request,
                                         const CancellationToken& cancellation) = 0;
};

/**
 * Per-surface safety and calibration policy.
 *
 * Direct execution is opt-in for each operation. Omitting an operation from
 * directPlanConfidenceByOperation means it can never use the direct path,
 * regardless of the model's confidence.
 */
struct FastInferenceConfig {
    bool enabled = false;
    bool allowToolNarrowing = true;
    std::size_t maxNarrowedTools = 8;
    float minimumNarrowingConfidence = 0.0f;
    std::map<juce::String, float> directPlanConfidenceByOperation;
};

struct FastInferenceTrace {
    bool evaluated = false;
    FastInferenceDecision predictedDecision = FastInferenceDecision::NeedsGeneralAgent;
    FastInferenceDecision appliedDecision = FastInferenceDecision::NeedsGeneralAgent;
    float confidence = 0.0f;
    std::chrono::microseconds latency{0};
    std::vector<juce::String> selectedTools;
    juce::String fallbackReason;
};

const char* toString(FastInferenceDecision decision);

}  // namespace magda::agent
