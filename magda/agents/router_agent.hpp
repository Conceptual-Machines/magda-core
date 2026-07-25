#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

namespace magda {

/**
 * @brief Lightweight router agent that classifies user intent.
 *
 * Returns a ConsoleIntent token (COMMAND, MUSIC, BOTH, AUTOMATION, DRUM,
 * MIXING, SESSION). Uses a cheap/fast model (configured via "router" agent
 * config), or — under provider::FAST_INFERENCE — the offline on-device
 * classifier in router_model.hpp, which makes the whole command path work with
 * no network (#1843).
 */
class RouterAgent {
  public:
    struct ClassifyResult {
        std::string intent;  // ConsoleIntent token; empty means "no opinion"
        double wallSeconds = 0.0;
        std::string error;
        bool hasError = false;
    };

    /** Classify user message intent (call from background thread). */
    ClassifyResult classify(const std::string& message);

    /** Run the offline on-device router model (provider::FAST_INFERENCE). */
    ClassifyResult classifyLocal(const std::string& message);

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
