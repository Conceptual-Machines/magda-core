#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "compact_parser.hpp"

namespace magda {

/**
 * @brief Command agent — handles DAW operations via compact IR.
 *
 * Generates: TRACK, DEL, MUTE, SOLO, SET, CLIP, FX.
 * Uses a cheap/fast model (configured via "command" agent config).
 * Receives DAW state snapshot for context.
 */
class CommandAgent {
  public:
    struct GenerateResult {
        std::string compactOutput;
        std::vector<Instruction> instructions;
        std::string error;
        bool hasError = false;
    };

    /** Generate command instructions from user message (background thread safe). */
    GenerateResult generate(const std::string& message);

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    static const char* getSystemPrompt();
    CompactParser parser_;
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
