#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Enhanced base for commands with state validation
 *
 * Extends UndoableCommand with:
 * - Precondition validation (canExecute)
 * - State consistency validation (validateState)
 * - Execution tracking
 */
class ValidatedCommand : public UndoableCommand {
  public:
    /**
     * Check if command can be executed (validates preconditions)
     */
    virtual bool canExecute() const {
        return true;
    }

    /**
     * Validate that state is consistent after execute/undo
     * Override to add domain-specific validation
     */
    virtual bool validateState() const {
        return true;
    }

    /**
     * Get whether this command has been executed
     */
    bool wasExecuted() const {
        return executed_;
    }

  protected:
    bool executed_ = false;
};

/**
 * @brief Command that stores complete state snapshots for reliable undo
 *
 * Usage:
 * ```cpp
 * struct ClipState {
 *     juce::String name;
 *     double length;
 *     std::vector<AudioSource> audioSources;
 * };
 *
 * class MySplitCommand : public SnapshotCommand<ClipState> {
 *   ClipState captureState() override {
 *     auto* clip = getClip();
 *     return {clip->name, clip->length, clip->audioSources};
 *   }
 *
 *   void restoreState(const ClipState& state) override {
 *     auto* clip = getClip();
 *     clip->name = state.name;
 *     clip->length = state.length;
 *     clip->audioSources = state.audioSources;
 *   }
 *
 *   void performAction() override {
 *     // Do the split
 *   }
 * };
 * ```
 */
template <typename StateT> class SnapshotCommand : public ValidatedCommand {
  public:
    void execute() override {
        if (!canExecute()) {
            return;
        }

        // Capture state before modification
        beforeState_ = captureState();

        // Perform the action
        performAction();

        // Capture state after modification
        afterState_ = captureState();

        // Validate consistency
        if (!validateState()) {
            // Rollback on validation failure
            restoreState(beforeState_);
            executed_ = false;  // Mark as not executed since we rolled back
            return;
        }

        executed_ = true;
    }

    void undo() override {
        if (!executed_) {
            return;
        }

        restoreState(beforeState_);

        if (!validateState()) {
            // State corrupted - try to restore after state
            restoreState(afterState_);
        }
    }

  protected:
    /**
     * Capture current state of the domain
     * Should return a complete snapshot that can restore state later
     */
    virtual StateT captureState() = 0;

    /**
     * Restore domain to a previously captured state
     */
    virtual void restoreState(const StateT& state) = 0;

    /**
     * Perform the actual command action
     * Called between before and after state captures
     */
    virtual void performAction() = 0;

  private:
    StateT beforeState_;
    StateT afterState_;
};

}  // namespace magda
