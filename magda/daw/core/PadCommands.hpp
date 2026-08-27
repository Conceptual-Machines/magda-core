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
 * Redo re-runs the edit rather than restoring the after-state, so an edit that
 * allocates ids (a device added to a pad) hands out the same ones it would have
 * on a fresh run.
 */
class EditPadsCommand : public UndoableCommand {
  public:
    /// @p edit runs against the live model, addressed by @p gridPath. It is
    /// held for redo, so it must be repeatable.
    ///
    /// @p mergeKey coalesces: two consecutive edits carrying the same non-empty
    /// key on the same grid collapse into one undo step, so a fader drag is one
    /// step back to where it started rather than one per mouse move. Empty
    /// never merges.
    EditPadsCommand(const ChainNodePath& gridPath, juce::String description,
                    std::function<void()> edit, juce::String mergeKey = {});

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return description_;
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ChainNodePath gridPath_;
    juce::String description_;
    std::function<void()> edit_;
    juce::String mergeKey_;
    PadRack padsBefore_;
    bool executed_ = false;
};

/// Run @p edit as one undoable step on @p gridPath's pads.
void editPads(const ChainNodePath& gridPath, const juce::String& description,
              std::function<void()> edit, const juce::String& mergeKey = {});

}  // namespace magda
