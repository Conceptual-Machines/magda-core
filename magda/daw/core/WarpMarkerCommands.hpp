#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "ClipInfo.hpp"
#include "UndoManager.hpp"

namespace magda {

// Marker reads include identity boundaries when the event has no authored map.
// Both coordinates are seconds, including for clips trimmed into their source.
std::vector<WarpMarker> getClipWarpMarkers(ClipId clipId);

/// Seed a newly warped clip with identity markers at the transients it shows, if detected.
void seedWarpMarkersFromTransients(ClipId clipId, double bpm);

/// Drop the clip's authored marker map, leaving the identity boundaries.
void clearWarpMarkers(ClipId clipId);

/**
 * @brief Command for adding a warp marker
 */
class AddWarpMarkerCommand : public UndoableCommand {
  public:
    AddWarpMarkerCommand(ClipId clipId, double sourceTime, double warpTime);

    juce::String getDescription() const override {
        return "Add Warp Marker";
    }

    void execute() override;
    void undo() override;

    int getAddedMarkerIndex() const {
        return addedIndex_;
    }

  private:
    std::optional<std::vector<WarpMarker>> oldMarkers_;
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
    MoveWarpMarkerCommand(ClipId clipId, int index, double newWarpTime);

    juce::String getDescription() const override {
        return "Move Warp Marker";
    }

    void execute() override;
    void undo() override;

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    std::optional<std::vector<WarpMarker>> oldMarkers_;
    ClipId clipId_;
    int index_;
    double newWarpTime_;
};

/**
 * @brief Command for removing a warp marker
 */
class RemoveWarpMarkerCommand : public UndoableCommand {
  public:
    RemoveWarpMarkerCommand(ClipId clipId, int index);

    juce::String getDescription() const override {
        return "Remove Warp Marker";
    }

    void execute() override;
    void undo() override;

  private:
    std::optional<std::vector<WarpMarker>> oldMarkers_;
    ClipId clipId_;
    int index_;
};

}  // namespace magda
