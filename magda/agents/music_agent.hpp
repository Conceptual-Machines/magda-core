#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "compact_parser.hpp"

namespace magda {

/**
 * @brief Music agent — generates pure musical content.
 *
 * Generates: CHORD, NOTE, ARP.
 * Has no knowledge of tracks, clips, plugins, or DAW state.
 * Uses an expensive/capable model (configured via "music" agent config).
 */
class MusicAgent {
  public:
    struct GenerateResult {
        std::string compactOutput;
        std::vector<Instruction> instructions;
        std::string error;
        bool hasError = false;
    };

    /** Generate music instructions from user message (background thread safe). */
    GenerateResult generate(const std::string& message);

    /** Streaming variant — calls onToken for each received token. */
    GenerateResult generateStreaming(const std::string& message, TokenCallback onToken);

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
