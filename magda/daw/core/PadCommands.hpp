#pragma once

#include <functional>

#include "DeviceInfo.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Any edit to a pad-per-chain device's pads, made undoable (#2211).
 *
 * The per-op commands in TrackCommands.cpp cannot serve a pad. A pad's address
 * spells its Rack step with the grid's own DeviceId, and rack ids and device
 * ids come out of counters that both start at 1, so `AddDeviceByPathCommand`
 * and friends would resolve a pad edit onto whichever rack happened to share
 * the number. Every pad edit therefore goes through `TrackManager`'s pad calls,
 * which reach the model through the owning device instead (#2207).
 *
 * The undo state is the whole of `DeviceInfo::pads`. That is one field on one
 * device, deep-copied by `PadRack`, and it is the only snapshot that stays
 * correct for the edits that touch more than one pad at a time: a swap trades
 * two chains' note ranges, dropping an instrument replaces a pad's whole chain,
 * and clearing one erases it. Recording those as per-op inverses would be a
 * second description of what each edit does, free to disagree with the first.
 *
 * The state is taken after the grid's live plugins have been captured into the
 * model, so undoing the removal of a pad restores it as it sounded rather than
 * as it was added.
 *
 * Both sides are snapshots. Redo restores the after-state rather than replaying
 * the edit, because `addDeviceToPad()` allocates a fresh DeviceId on every run:
 * a replayed add would bring the device back under a new id and leave every
 * later command in the redo chain naming the old one.
 */
class EditPadsCommand : public UndoableCommand {
  public:
    /// @p edit runs against the live model, addressed by @p gridPath. It runs
    /// once: redo restores the snapshot it produced.
    EditPadsCommand(const ChainNodePath& gridPath, juce::String description,
                    std::function<void()> edit);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return description_;
    }

  private:
    ChainNodePath gridPath_;
    juce::String description_;
    std::function<void()> edit_;
    PadRack padsBefore_;
    PadRack padsAfter_;
    bool executed_ = false;
};

/// Run @p edit as one undoable step on @p gridPath's pads.
void editPads(const ChainNodePath& gridPath, const juce::String& description,
              std::function<void()> edit);

/**
 * @brief A pad's fader or pan, undone by putting the old value back (#2211).
 *
 * Separate from `EditPadsCommand` because a fader ticks and the sound has to
 * follow the mouse, so this is the one pad edit that cannot wait for the drag
 * to end. Taking a pad-rack snapshot per mouse move would mean flushing every
 * pad plugin's live state into the model to make the snapshot honest and then
 * deep-copying the whole rack, which is the right price for a structural edit
 * and far too much for a mouse move.
 *
 * One value in, one value out, which is the shape `SetTrackVolumeCommand`
 * already has for a track's fader.
 */
class SetPadFaderCommand : public UndoableCommand {
  public:
    enum class Target { Volume, Pan };

    /// @p gesture bounds the merging. Two edits coalesce only while they carry
    /// the same one, so a drag is a single undo step and the next drag on the
    /// same fader is another. `UndoManager` merges adjacent commands with no
    /// timeout of its own, so without this one Undo would walk back every
    /// gesture since something else last intervened.
    SetPadFaderCommand(const ChainNodePath& gridPath, int padIndex, Target target, float value,
                       int gesture);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override;

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    void apply(float value) const;

    ChainNodePath gridPath_;
    int padIndex_ = -1;
    Target target_ = Target::Volume;
    float oldValue_ = 0.0f;
    float newValue_ = 0.0f;
    int gesture_ = 0;
    bool valid_ = false;
};

/// Set a pad's fader or pan as one undoable step, coalescing within @p gesture.
void setPadFader(const ChainNodePath& gridPath, int padIndex, SetPadFaderCommand::Target target,
                 float value, int gesture);

}  // namespace magda
