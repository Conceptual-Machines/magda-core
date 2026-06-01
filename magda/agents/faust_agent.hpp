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
    struct Result {
        std::string name;         // short DSP name, e.g. "Tape Saturator"
        std::string description;  // one-line musical description
        std::string source;       // .dsp source code
        std::string rawOutput;    // raw LLM text, kept for diagnostics
        std::string error;
        bool hasError = false;
    };

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
    static const char* getSystemPrompt();
    Result parseJson(const juce::String& text);

    // Compile the source through the faust-mcp compile_faust tool. Returns true
    // if it compiles (or if no MCP server is configured, so generation isn't
    // blocked); on failure fills errorOut with the compiler message.
    bool compileCheck(const std::string& name, const std::string& source, std::string& errorOut);

    // Shared body for generate / generateStreaming. Runs the conversational
    // generate-and-auto-fix loop: on a compile failure the broken reply is
    // already the last assistant turn, so the error is fed back as the next
    // user turn, up to kMaxAttempts. Empty onToken = non-streaming.
    Result runConversational(const std::string& message, llm::Conversation& conversation,
                             TokenCallback onToken);

    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
