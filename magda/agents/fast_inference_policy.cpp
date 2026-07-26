#include "fast_inference_policy.hpp"

namespace magda::agent {

const char* toString(FastInferenceDecision decision) {
    switch (decision) {
        case FastInferenceDecision::DirectPlan:
            return "direct_plan";
        case FastInferenceDecision::NarrowAgentTools:
            return "narrow_agent_tools";
        case FastInferenceDecision::NeedsGeneralAgent:
            return "needs_general_agent";
        case FastInferenceDecision::Ambiguous:
            return "ambiguous";
        case FastInferenceDecision::Unsupported:
            return "unsupported";
    }
    return "needs_general_agent";
}

}  // namespace magda::agent
