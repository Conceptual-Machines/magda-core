#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "compact_parser.hpp"

namespace magda {

/**
 * @brief Drummer agent — generates drum patterns by role.
 *
 * Emits the grid grammar described in DrumGridRoles.hpp: one line per role,
 * cells separated by spaces, '.' = rest, 'x' = hit, 'X' = accent. The role
 * vocabulary is closed (see DrumGridRoles.hpp). The agent has no knowledge of
 * the kit mapping — at execution time the InstructionExecutor resolves each
 * role to the MIDI note number of the matching row on the focused track's
 * primary-instrument kit.
 */
class DrummerAgent {
  public:
    struct GenerateResult {
        std::string rawOutput;
        std::string description;
        std::vector<Instruction> instructions;
        std::string error;
        bool hasError = false;
    };

    /** Generate drum instructions from a user message (background thread). */
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
