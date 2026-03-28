#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

namespace magda {

/**
 * @brief Command agent — handles DAW operations via DSL generation.
 *
 * Generates DSL code (track/clip/fx/notes operations).
 * Uses a cheap/fast model (configured via "command" agent config).
 * Receives DAW state snapshot for context.
 */
class CommandAgent {
  public:
    struct GenerateResult {
        std::string dslOutput;  // raw DSL text from the LLM
        std::string error;
        bool hasError = false;
    };

    using TokenCallback = std::function<bool(const juce::String&)>;

    /** Generate DSL from user message (background thread safe). */
    GenerateResult generate(const std::string& message);

    /** Streaming variant — calls onToken for each received token. */
    GenerateResult generateStreaming(const std::string& message, TokenCallback onToken);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

    static const char* getSystemPrompt();

  private:
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
