#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

#include "compact_parser.hpp"  // for TokenCallback

namespace magda {

/**
 * @brief Thin LLM wrapper for theme generation.
 *
 * Deliberately knows nothing about colours, roles, or the theme file format —
 * that domain knowledge lives in the UI themes layer (ThemePrompt.hpp), which
 * builds the system prompt and validates the result. This class only owns the
 * LLM round-trip + cancellation, so the agents library stays free of any UI
 * dependency. The caller supplies the (theme-specific) system prompt.
 *
 * Reuses role::MUSIC for the LLM config so users don't have to configure a
 * separate provider just to generate themes.
 */
class ThemeAgent {
  public:
    struct Result {
        std::string text;  // raw LLM output
        std::string error;
        bool hasError = false;
    };

    /** Run one prompt->text turn (background-thread safe). When `schema` is a
     *  non-void JSON schema, it is forwarded as the request's structured-output
     *  schema so providers that support it (Anthropic, OpenAI, Gemini)
     *  constrain the response to valid JSON. */
    Result generate(const std::string& systemPrompt, const std::string& userMessage,
                    const juce::var& schema = {});

    /** Streaming variant: `onToken` fires per token as the model emits the JSON.
     *  The full text is still returned so the caller can validate it once the
     *  stream completes. */
    Result generateStreaming(const std::string& systemPrompt, const std::string& userMessage,
                             const juce::var& schema, TokenCallback onToken);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
