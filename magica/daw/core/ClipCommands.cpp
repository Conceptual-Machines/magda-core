#include "ClipCommands.hpp"

#include <iostream>

namespace magica {

// ============================================================================
// SplitClipCommand
// ============================================================================

SplitClipCommand::SplitClipCommand(ClipId clipId, double splitTime)
    : originalClipId_(clipId), splitTime_(splitTime) {}

void SplitClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(originalClipId_);

    if (!clip) {
        std::cout << "📝 UNDO: SplitClipCommand::execute - clip not found" << std::endl;
        return;
    }

    // Store state for undo (only on first execute)
    if (!executed_) {
        originalName_ = clip->name;
        originalLength_ = clip->length;
    }

    // Perform the split
    createdClipId_ = clipManager.splitClip(originalClipId_, splitTime_);
    executed_ = true;

    std::cout << "📝 UNDO: Split clip " << originalClipId_ << " at " << splitTime_
              << " -> new clip " << createdClipId_ << std::endl;
}

void SplitClipCommand::undo() {
    if (!executed_ || createdClipId_ == INVALID_CLIP_ID) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();

    // Delete the created right clip
    clipManager.deleteClip(createdClipId_);

    // Restore original clip's properties
    if (auto* clip = clipManager.getClip(originalClipId_)) {
        clip->name = originalName_;
        clip->length = originalLength_;
    }

    // Force UI refresh after direct property modification
    clipManager.forceNotifyClipsChanged();

    std::cout << "📝 UNDO: Undid split - deleted clip " << createdClipId_ << ", restored clip "
              << originalClipId_ << std::endl;
}

// ============================================================================
// MoveClipCommand
// ============================================================================

MoveClipCommand::MoveClipCommand(ClipId clipId, double newStartTime)
    : clipId_(clipId), newStartTime_(newStartTime) {}

void MoveClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId_);

    if (!clip) {
        return;
    }

    // Store old position (only on first execute)
    if (!executed_) {
        oldStartTime_ = clip->startTime;
    }

    clipManager.moveClip(clipId_, newStartTime_);
    executed_ = true;
}

void MoveClipCommand::undo() {
    if (!executed_) {
        return;
    }

    ClipManager::getInstance().moveClip(clipId_, oldStartTime_);
}

bool MoveClipCommand::canMergeWith(const UndoableCommand* other) const {
    // Merge with subsequent moves of the same clip
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    return otherMove != nullptr && otherMove->clipId_ == clipId_;
}

void MoveClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    if (otherMove) {
        // Keep our oldStartTime, update to their newStartTime
        newStartTime_ = otherMove->newStartTime_;
    }
}

// ============================================================================
// MoveClipToTrackCommand
// ============================================================================

MoveClipToTrackCommand::MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId)
    : clipId_(clipId), newTrackId_(newTrackId) {}

void MoveClipToTrackCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId_);

    if (!clip) {
        return;
    }

    // Store old track (only on first execute)
    if (!executed_) {
        oldTrackId_ = clip->trackId;
    }

    clipManager.moveClipToTrack(clipId_, newTrackId_);
    executed_ = true;
}

void MoveClipToTrackCommand::undo() {
    if (!executed_) {
        return;
    }

    ClipManager::getInstance().moveClipToTrack(clipId_, oldTrackId_);
}

// ============================================================================
// ResizeClipCommand
// ============================================================================

ResizeClipCommand::ResizeClipCommand(ClipId clipId, double newLength, bool fromStart)
    : clipId_(clipId), newLength_(newLength), fromStart_(fromStart) {}

void ResizeClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId_);

    if (!clip) {
        return;
    }

    // Store old state (only on first execute)
    if (!executed_) {
        oldStartTime_ = clip->startTime;
        oldLength_ = clip->length;
    }

    // Calculate the new start time if resizing from start
    if (fromStart_) {
        newStartTime_ = oldStartTime_ + (oldLength_ - newLength_);
    } else {
        newStartTime_ = clip->startTime;
    }

    clipManager.resizeClip(clipId_, newLength_, fromStart_);
    executed_ = true;
}

void ResizeClipCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();

    // Restore both start time and length
    if (auto* clip = clipManager.getClip(clipId_)) {
        clip->startTime = oldStartTime_;
        clip->length = oldLength_;
    }

    // Force UI refresh after direct property modification
    clipManager.forceNotifyClipsChanged();
}

bool ResizeClipCommand::canMergeWith(const UndoableCommand* other) const {
    // Merge with subsequent resizes of the same clip with same direction
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    return otherResize != nullptr && otherResize->clipId_ == clipId_ &&
           otherResize->fromStart_ == fromStart_;
}

void ResizeClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    if (otherResize) {
        // Keep our old state, update to their new state
        newLength_ = otherResize->newLength_;
        newStartTime_ = otherResize->newStartTime_;
    }
}

// ============================================================================
// DeleteClipCommand
// ============================================================================

DeleteClipCommand::DeleteClipCommand(ClipId clipId) : clipId_(clipId) {}

void DeleteClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId_);

    if (!clip) {
        return;
    }

    // Store full clip info for undo (only on first execute)
    if (!executed_) {
        storedClip_ = *clip;
    }

    clipManager.deleteClip(clipId_);
    executed_ = true;

    std::cout << "📝 UNDO: Deleted clip " << clipId_ << std::endl;
}

void DeleteClipCommand::undo() {
    if (!executed_) {
        return;
    }

    ClipManager::getInstance().restoreClip(storedClip_);

    std::cout << "📝 UNDO: Restored clip " << clipId_ << std::endl;
}

// ============================================================================
// CreateClipCommand
// ============================================================================

CreateClipCommand::CreateClipCommand(ClipType type, TrackId trackId, double startTime,
                                     double length, const juce::String& audioFilePath)
    : type_(type),
      trackId_(trackId),
      startTime_(startTime),
      length_(length),
      audioFilePath_(audioFilePath) {}

void CreateClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    if (type_ == ClipType::Audio) {
        createdClipId_ = clipManager.createAudioClip(trackId_, startTime_, length_, audioFilePath_);
    } else {
        createdClipId_ = clipManager.createMidiClip(trackId_, startTime_, length_);
    }

    executed_ = true;
}

void CreateClipCommand::undo() {
    if (!executed_ || createdClipId_ == INVALID_CLIP_ID) {
        return;
    }

    ClipManager::getInstance().deleteClip(createdClipId_);
}

// ============================================================================
// DuplicateClipCommand
// ============================================================================

DuplicateClipCommand::DuplicateClipCommand(ClipId sourceClipId, double startTime,
                                           TrackId targetTrackId)
    : sourceClipId_(sourceClipId), startTime_(startTime), targetTrackId_(targetTrackId) {}

void DuplicateClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    if (startTime_ < 0) {
        // Use default position (right after source)
        duplicatedClipId_ = clipManager.duplicateClip(sourceClipId_);
    } else {
        duplicatedClipId_ = clipManager.duplicateClipAt(sourceClipId_, startTime_, targetTrackId_);
    }

    executed_ = true;
}

void DuplicateClipCommand::undo() {
    if (!executed_ || duplicatedClipId_ == INVALID_CLIP_ID) {
        return;
    }

    ClipManager::getInstance().deleteClip(duplicatedClipId_);
}

}  // namespace magica
