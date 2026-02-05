#include "WarpMarkerCommands.hpp"

#include "../audio/AudioBridge.hpp"

namespace magda {

// =============================================================================
// AddWarpMarkerCommand
// =============================================================================

AddWarpMarkerCommand::AddWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, double sourceTime,
                                           double warpTime)
    : bridge_(bridge), clipId_(clipId), sourceTime_(sourceTime), warpTime_(warpTime) {}

void AddWarpMarkerCommand::execute() {
    if (!bridge_)
        return;

    addedIndex_ = bridge_->addWarpMarker(clipId_, sourceTime_, warpTime_);
}

void AddWarpMarkerCommand::undo() {
    if (!bridge_ || addedIndex_ < 0)
        return;

    bridge_->removeWarpMarker(clipId_, addedIndex_);
    addedIndex_ = -1;
}

// =============================================================================
// MoveWarpMarkerCommand
// =============================================================================

MoveWarpMarkerCommand::MoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index,
                                             double newWarpTime)
    : bridge_(bridge), clipId_(clipId), index_(index), newWarpTime_(newWarpTime) {}

void MoveWarpMarkerCommand::execute() {
    if (!bridge_)
        return;

    // Capture old position if we haven't already
    if (!hasOldTime_) {
        auto markers = bridge_->getWarpMarkers(clipId_);
        if (index_ >= 0 && index_ < static_cast<int>(markers.size())) {
            oldWarpTime_ = markers[static_cast<size_t>(index_)].warpTime;
            hasOldTime_ = true;
        }
    }

    bridge_->moveWarpMarker(clipId_, index_, newWarpTime_);
}

void MoveWarpMarkerCommand::undo() {
    if (!bridge_ || !hasOldTime_)
        return;

    bridge_->moveWarpMarker(clipId_, index_, oldWarpTime_);
}

bool MoveWarpMarkerCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other);
    if (!otherMove)
        return false;

    // Merge consecutive moves of the same marker
    return clipId_ == otherMove->clipId_ && index_ == otherMove->index_;
}

void MoveWarpMarkerCommand::mergeWith(const UndoableCommand* other) {
    auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other);
    if (otherMove) {
        // Keep our oldWarpTime_, update newWarpTime_ to the latest
        newWarpTime_ = otherMove->newWarpTime_;
    }
}

// =============================================================================
// RemoveWarpMarkerCommand
// =============================================================================

RemoveWarpMarkerCommand::RemoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index)
    : bridge_(bridge), clipId_(clipId), index_(index) {}

void RemoveWarpMarkerCommand::execute() {
    if (!bridge_)
        return;

    // Capture state before removal
    if (!hasCapturedState_) {
        auto markers = bridge_->getWarpMarkers(clipId_);
        if (index_ >= 0 && index_ < static_cast<int>(markers.size())) {
            removedSourceTime_ = markers[static_cast<size_t>(index_)].sourceTime;
            removedWarpTime_ = markers[static_cast<size_t>(index_)].warpTime;
            hasCapturedState_ = true;
        }
    }

    bridge_->removeWarpMarker(clipId_, index_);
}

void RemoveWarpMarkerCommand::undo() {
    if (!bridge_ || !hasCapturedState_)
        return;

    // Re-add the marker at its original position
    bridge_->addWarpMarker(clipId_, removedSourceTime_, removedWarpTime_);
}

}  // namespace magda
