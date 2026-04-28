#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "automation_parser.hpp"

namespace magda {

class MagdaApi;

/**
 * @brief Executes AutomationAgent IR against the MagdaApi automation surface.
 *
 * Resolves the "selected" target via the host's selection state, generates
 * shape points in beats, and writes them through MagdaApi::automation().
 *
 * MUST be called on the message thread (host writes ultimately notify UI
 * listeners).
 */
class AutomationExecutor {
  public:
    explicit AutomationExecutor(MagdaApi& api) : api_(api) {}

    /** Run all instructions. Returns true on success. */
    bool execute(const std::vector<AutoInstruction>& instructions);

    juce::String getError() const {
        return error_;
    }
    juce::String getResults() const {
        return results_;
    }

  private:
    MagdaApi& api_;
    juce::String error_;
    juce::String results_;
};

}  // namespace magda
