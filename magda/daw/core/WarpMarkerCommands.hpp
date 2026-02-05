#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ClipTypes.hpp"
#include "UndoManager.hpp"

namespace magda {

// Forward declare AudioBridge
class AudioBridge;

/**
 * @brief Warp marker info for state capture
 */
struct WarpMarkerState {
    double sourceTime;
    double warpTime;
};

/**
 * @brief Command for adding a warp marker
 */
class AddWarpMarkerCommand : public UndoableCommand {
  public:
    AddWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, double sourceTime, double warpTime);

    juce::String getDescription() const override {
        return "Add Warp Marker";
    }

    void execute() override;
    void undo() override;

    int getAddedMarkerIndex() const {
        return addedIndex_;
    }

  private:
    AudioBridge* bridge_;
    ClipId clipId_;
    double sourceTime_;
    double warpTime_;
    int addedIndex_ = -1;
};

/**
 * @brief Command for moving a warp marker
 */
class MoveWarpMarkerCommand : public UndoableCommand {
  public:
    MoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index, double newWarpTime);

    juce::String getDescription() const override {
        return "Move Warp Marker";
    }

    void execute() override;
    void undo() override;

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    AudioBridge* bridge_;
    ClipId clipId_;
    int index_;
    double oldWarpTime_;
    double newWarpTime_;
    bool hasOldTime_ = false;
};

/**
 * @brief Command for removing a warp marker
 */
class RemoveWarpMarkerCommand : public UndoableCommand {
  public:
    RemoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index);

    juce::String getDescription() const override {
        return "Remove Warp Marker";
    }

    void execute() override;
    void undo() override;

  private:
    AudioBridge* bridge_;
    ClipId clipId_;
    int index_;
    double removedSourceTime_;
    double removedWarpTime_;
    bool hasCapturedState_ = false;
};

}  // namespace magda
