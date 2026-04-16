#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "automation_executor.hpp"
#include "automation_parser.hpp"

namespace magda {

/**
 * @brief Automation agent — emits automation curves on a selected lane.
 *
 * Generates AUTO instructions (shape-based: sin, tri, saw, square, exp, log,
 * line, plus freeform). Time domain is beats. Values are normalized [0, 1].
 *
 * processMessage() flow:
 *   1. Call LLM with system prompt describing AUTO grammar + current
 *      selection context
 *   2. Parse response into AutoInstruction IR
 *   3. Execute against AutomationManager (must be on message thread)
 */
class AutomationAgent {
  public:
    struct GenerateResult {
        std::string rawOutput;
        std::vector<AutoInstruction> instructions;
        std::string error;
        bool hasError = false;
    };

    using TokenCallback = std::function<bool(const juce::String&)>;

    /** Call LLM to produce AUTO instructions (background-thread safe). */
    GenerateResult generate(const std::string& message);

    /** Streaming variant — token callback fires for each streamed chunk. */
    GenerateResult generateStreaming(const std::string& message, TokenCallback onToken);

    /** Execute previously-parsed IR. MUST be called on the message thread. */
    std::string execute(const GenerateResult& result);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

    static const char* getSystemPrompt();

  private:
    AutomationParser parser_;
    AutomationExecutor executor_;
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
