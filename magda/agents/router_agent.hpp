#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

namespace magda {

/**
 * @brief Lightweight router agent that classifies user intent.
 *
 * Returns one of: COMMAND, MUSIC, BOTH.
 * Uses a cheap/fast model (configured via "router" agent config).
 */
class RouterAgent {
  public:
    struct ClassifyResult {
        std::string intent;  // "COMMAND", "MUSIC", or "BOTH"
        double wallSeconds = 0.0;
        std::string error;
        bool hasError = false;
    };

    /** Classify user message intent (call from background thread). */
    ClassifyResult classify(const std::string& message);

    /** Signal cancellation. */
    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    static const char* getSystemPrompt();
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
