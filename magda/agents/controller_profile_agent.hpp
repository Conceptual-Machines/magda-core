#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "../daw/core/controllers/ControllerProfile.hpp"

namespace magda {

/**
 * @brief Generates a ControllerProfile JSON from a natural-language description.
 *
 * Uses the "controller" AgentLLMConfig role. Structured output (Request::schema) is
 * used to constrain the model to MAGDA's ControllerProfile shape so the return is
 * decodable in one pass. Invocation is synchronous — call from a background thread.
 */
class ControllerProfileAgent {
  public:
    struct Result {
        std::optional<ControllerProfile> profile;
        juce::String rawJson;  // raw LLM output for debugging / saving to disk
        double wallSeconds = 0.0;
        std::string error;
        bool hasError = false;
    };

    /** Generate a profile from a natural-language description.
     *
     * livePortNames is used as context — the LLM knows what MIDI devices
     * are connected and can name them accurately in the generated profile.
     * Pass empty to skip that hint.
     */
    Result generate(const std::string& description,
                    const std::vector<std::string>& livePortNames = {});

    /** Signal cancellation. */
    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    static const char* getSystemPrompt();
    static juce::var buildSchema();
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
