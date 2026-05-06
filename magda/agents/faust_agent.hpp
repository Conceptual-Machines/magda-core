#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

#include "compact_parser.hpp"  // for TokenCallback

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

    Result generate(const std::string& message);
    Result generateStreaming(const std::string& message, TokenCallback onToken);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    static const char* getSystemPrompt();
    Result parseJson(const juce::String& text);
    Result validateWithMCP(Result result);
    juce::String buildUserMessage(const std::string& message) const;

    std::atomic<bool> shouldStop_{false};
    std::string lastFailedSource_;
    std::string lastCompileError_;
};

}  // namespace magda
