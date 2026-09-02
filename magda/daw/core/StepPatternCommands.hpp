#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <utility>

#include "ChainNodePath.hpp"
#include "CommandPattern.hpp"
#include "StepPatternState.hpp"

namespace magda {

/**
 * @brief Undoable step-pattern edits on a sequencer device.
 *
 * A pattern is authored state that lives in the model's device state document
 * (#2313), so an edit is a patch to that document and undo puts the whole
 * previous document back - the same shape as the other authored-state commands
 * in DeviceStateCommands.hpp, and for the same reason: the snapshot IS the
 * model's document, so undo works whether or not a live device exists.
 *
 * One whole-pattern command covers every edit either surface makes. A pattern
 * is small, and the alternative - a command per field - would be more code for
 * a coarser result: step toggles are exactly the edit users spam and expect to
 * undo in one go. The caller reads the current pattern, changes what it wants,
 * and hands the result over.
 */

/// The pattern the model holds for the sequencer at @p devicePath. An empty
/// default when the path names no step sequencer.
step_pattern::MonoPattern currentMonoPattern(const ChainNodePath& devicePath);
step_pattern::PolyPattern currentPolyPattern(const ChainNodePath& devicePath);

/**
 * Whether an edit is one step of a continuous gesture.
 *
 * A velocity or probability drag writes the pattern on every mouse move; each
 * write is a command, and without this the undo stack would fill with a step of
 * the drag rather than the drag. Coalescing folds a run of them into the one
 * entry that undoes back to where the gesture started. A discrete edit - a step
 * toggled, a note placed - never coalesces: each is its own undo.
 */
enum class StepPatternGesture { Discrete, Continuous };

/// Replace a monophonic step sequencer's whole pattern.
class SetMonoStepPatternCommand : public SnapshotCommand<juce::String> {
  public:
    SetMonoStepPatternCommand(const ChainNodePath& devicePath, step_pattern::MonoPattern pattern,
                              juce::String description,
                              StepPatternGesture gesture = StepPatternGesture::Discrete);

    juce::String getDescription() const override;
    bool canExecute() const override;
    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  protected:
    juce::String captureState() override;
    void restoreState(const juce::String& state) override;
    void performAction() override;

  private:
    ChainNodePath devicePath_;
    step_pattern::MonoPattern pattern_;
    juce::String description_;
    StepPatternGesture gesture_;
};

/// Replace a polyphonic step sequencer's whole pattern.
class SetPolyStepPatternCommand : public SnapshotCommand<juce::String> {
  public:
    SetPolyStepPatternCommand(const ChainNodePath& devicePath, step_pattern::PolyPattern pattern,
                              juce::String description,
                              StepPatternGesture gesture = StepPatternGesture::Discrete);

    juce::String getDescription() const override;
    bool canExecute() const override;
    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  protected:
    juce::String captureState() override;
    void restoreState(const juce::String& state) override;
    void performAction() override;

  private:
    ChainNodePath devicePath_;
    step_pattern::PolyPattern pattern_;
    juce::String description_;
    StepPatternGesture gesture_;
};

/**
 * Run @p edit over the device's current pattern and commit the result as one
 * undoable step. Returns false when the path names no such device, or when the
 * edit left the pattern as it found it - nothing is recorded either way, so a
 * no-op gesture does not leave a dead entry on the undo stack.
 */
bool editMonoStepPattern(const ChainNodePath& devicePath, const juce::String& description,
                         const std::function<void(step_pattern::MonoPattern&)>& edit,
                         StepPatternGesture gesture = StepPatternGesture::Discrete);
bool editPolyStepPattern(const ChainNodePath& devicePath, const juce::String& description,
                         const std::function<void(step_pattern::PolyPattern&)>& edit,
                         StepPatternGesture gesture = StepPatternGesture::Discrete);

/**
 * Replace a sequencer's pattern with a random one, as a single undoable step.
 *
 * The rules are the retired devices': roughly two steps in three play, on notes
 * from a two-octave range, with the occasional accent and glide (mono) or one
 * to three notes a step (poly).
 */
bool randomizeMonoStepPattern(const ChainNodePath& devicePath);
bool randomizePolyStepPattern(const ChainNodePath& devicePath);

}  // namespace magda
