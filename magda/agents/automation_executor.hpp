#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "automation_parser.hpp"

namespace magda {

/**
 * @brief Executes AutomationAgent IR against AutomationManager.
 *
 * Resolves the "selected" target from SelectionManager, generates shape
 * points in beats, and writes them via AutomationManager::addPoint.
 *
 * MUST be called on the message thread (touches AutomationManager which
 * notifies UI listeners).
 */
class AutomationExecutor {
  public:
    /** Run all instructions. Returns true on success. */
    bool execute(const std::vector<AutoInstruction>& instructions);

    juce::String getError() const {
        return error_;
    }
    juce::String getResults() const {
        return results_;
    }

  private:
    juce::String error_;
    juce::String results_;
};

}  // namespace magda
