#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

#include "compact_parser.hpp"  // for TokenCallback

namespace llm {
struct Conversation;
}

namespace magda {

class FaustAgent {
  public:
    enum class Target { Effect, Instrument };

    struct Result {
        std::string name;         // short DSP name, e.g. "Tape Saturator"
        std::string description;  // one-line musical description
        std::string source;       // .dsp source code
        std::string rawOutput;    // raw LLM text, kept for diagnostics
        std::string error;
        bool hasError = false;
        // True only after compile_faust returned a successful MCP tool result.
        bool mcpVerified = false;
    };

    explicit FaustAgent(Target target = Target::Effect) : target_(target) {}

    /// Validate target-specific control conventions before MCP compilation.
    static bool validateSource(Target target, const std::string& source, std::string& errorOut);

    /// The system prompt for a target. Public so tests can hold the worked
    /// examples inside it to the same contract validateSource enforces: an
    /// example that breaks the rules teaches every model to break them, and
    /// smaller models copy the examples over the prose.
    static const char* getSystemPrompt(Target target);

    // `conversation` carries the running multi-turn history (prior generated
    // DSP, fix attempts) so refinements edit the existing code rather than
    // starting blank. It is updated in place with the turns from this call.
    Result generate(const std::string& message, llm::Conversation& conversation);
    Result generateStreaming(const std::string& message, llm::Conversation& conversation,
                             TokenCallback onToken);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    Result parseJson(const juce::String& text);

    // Compile through faust-mcp. A disabled server intentionally stages code
    // for manual editing; an enabled server that cannot start or verify fails
    // closed and never reaches the live apply path.
    bool compileCheck(const std::string& name, const std::string& source, std::string& errorOut,
                      bool& verified);

    // Shared body for generate / generateStreaming. Runs the conversational
    // generate-and-auto-fix loop: on a compile failure the broken reply is
    // already the last assistant turn, so the error is fed back as the next
    // user turn, up to kMaxAttempts. Empty onToken = non-streaming.
    Result runConversational(const std::string& message, llm::Conversation& conversation,
                             TokenCallback onToken);

    std::atomic<bool> shouldStop_{false};
    Target target_;
};

}  // namespace magda
