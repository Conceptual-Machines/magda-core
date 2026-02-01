#include "ClipCommands.hpp"

#include <iostream>

namespace magda {

// ============================================================================
// SplitClipCommand
// ============================================================================

SplitClipCommand::SplitClipCommand(ClipId clipId, double splitTime)
    : clipId_(clipId), splitTime_(splitTime) {}

bool SplitClipCommand::canExecute() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip && splitTime_ > clip->startTime && splitTime_ < clip->getEndTime();
}

ClipInfo SplitClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void SplitClipCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();

    // Delete the right clip if it exists
    if (rightClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(rightClipId_);
        rightClipId_ = INVALID_CLIP_ID;
    }

    // Restore original clip completely
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;  // Full restoration - no missing fields!
        clipManager.forceNotifyClipsChanged();
    }
}

void SplitClipCommand::performAction() {
    rightClipId_ = ClipManager::getInstance().splitClip(clipId_, splitTime_);

    std::cout << "📝 UNDO: Split clip " << clipId_ << " at " << splitTime_ << " -> new clip "
              << rightClipId_ << std::endl;
}

bool SplitClipCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    // Validate left clip exists and has correct track
    auto* leftClip = clipManager.getClip(clipId_);
    if (!leftClip) {
        return false;
    }

    // Validate clip has a valid track
    if (leftClip->trackId == INVALID_TRACK_ID) {
        std::cerr << "ERROR: Clip " << clipId_ << " has invalid track!" << std::endl;
        return false;
    }

    // If executed, validate right clip exists
    if (executed_ && rightClipId_ != INVALID_CLIP_ID) {
        auto* rightClip = clipManager.getClip(rightClipId_);
        if (!rightClip) {
            return false;
        }

        // Validate right clip has valid track
        if (rightClip->trackId == INVALID_TRACK_ID) {
            std::cerr << "ERROR: Right clip " << rightClipId_ << " has invalid track!" << std::endl;
            return false;
        }

        // Validate clips are adjacent and continuous
        if (std::abs(leftClip->getEndTime() - rightClip->startTime) > 0.001) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// MoveClipCommand
// ============================================================================

MoveClipCommand::MoveClipCommand(ClipId clipId, double newStartTime)
    : clipId_(clipId), newStartTime_(newStartTime) {}

ClipInfo MoveClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void MoveClipCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;
        clipManager.forceNotifyClipsChanged();
    }
}

void MoveClipCommand::performAction() {
    ClipManager::getInstance().moveClip(clipId_, newStartTime_);
}

bool MoveClipCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    return otherMove != nullptr && otherMove->clipId_ == clipId_;
}

void MoveClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    if (otherMove) {
        // Update to their new position
        newStartTime_ = otherMove->newStartTime_;
    }
}

// ============================================================================
// MoveClipToTrackCommand
// ============================================================================

MoveClipToTrackCommand::MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId)
    : clipId_(clipId), newTrackId_(newTrackId) {}

bool MoveClipToTrackCommand::canExecute() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip && newTrackId_ != INVALID_TRACK_ID;
}

ClipInfo MoveClipToTrackCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void MoveClipToTrackCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;
        clipManager.forceNotifyClipsChanged();
    }
}

void MoveClipToTrackCommand::performAction() {
    ClipManager::getInstance().moveClipToTrack(clipId_, newTrackId_);
}

bool MoveClipToTrackCommand::validateState() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip) {
        return false;
    }

    // Critical: ensure clip has valid track
    if (clip->trackId == INVALID_TRACK_ID) {
        std::cerr << "ERROR: Clip " << clipId_ << " has invalid track after move!" << std::endl;
        return false;
    }

    return true;
}

// ============================================================================
// ResizeClipCommand
// ============================================================================

ResizeClipCommand::ResizeClipCommand(ClipId clipId, double newLength, bool fromStart)
    : clipId_(clipId), newLength_(newLength), fromStart_(fromStart) {}

ClipInfo ResizeClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void ResizeClipCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;
        clipManager.forceNotifyClipsChanged();
    }
}

void ResizeClipCommand::performAction() {
    ClipManager::getInstance().resizeClip(clipId_, newLength_, fromStart_);
}

bool ResizeClipCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    return otherResize != nullptr && otherResize->clipId_ == clipId_ &&
           otherResize->fromStart_ == fromStart_;
}

void ResizeClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    if (otherResize) {
        // Update to their new length
        newLength_ = otherResize->newLength_;
    }
}

// ============================================================================
// DeleteClipCommand
// ============================================================================

DeleteClipCommand::DeleteClipCommand(ClipId clipId) : clipId_(clipId) {}

ClipInfo DeleteClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void DeleteClipCommand::restoreState(const ClipInfo& state) {
    ClipManager::getInstance().restoreClip(state);
    std::cout << "📝 UNDO: Restored clip " << clipId_ << std::endl;
}

void DeleteClipCommand::performAction() {
    ClipManager::getInstance().deleteClip(clipId_);
    std::cout << "📝 UNDO: Deleted clip " << clipId_ << std::endl;
}

bool DeleteClipCommand::validateState() const {
    // Deletion is always valid - the clip state is stored in the snapshot
    // No need to validate since restoreState() handles both cases (clip exists/doesn't exist)
    return true;
}

// ============================================================================
// CreateClipCommand
// ============================================================================

CreateClipCommand::CreateClipCommand(ClipType type, TrackId trackId, double startTime,
                                     double length, const juce::String& audioFilePath,
                                     ClipView view)
    : type_(type),
      trackId_(trackId),
      startTime_(startTime),
      length_(length),
      audioFilePath_(audioFilePath),
      view_(view) {}

bool CreateClipCommand::canExecute() const {
    return trackId_ != INVALID_TRACK_ID && length_ > 0.0;
}

CreateClipState CreateClipCommand::captureState() {
    CreateClipState state;
    state.createdClipId = createdClipId_;
    state.wasCreated = (createdClipId_ != INVALID_CLIP_ID);
    return state;
}

void CreateClipCommand::restoreState(const CreateClipState& state) {
    auto& clipManager = ClipManager::getInstance();

    // If we're restoring to a state where clip didn't exist, delete current clip
    if (!state.wasCreated && createdClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(createdClipId_);
        createdClipId_ = INVALID_CLIP_ID;
    }
    // If restoring to a state where it did exist, recreate it (redo)
    else if (state.wasCreated && state.createdClipId != INVALID_CLIP_ID &&
             createdClipId_ == INVALID_CLIP_ID) {
        // Redo: recreate the clip
        performAction();
    }
}

void CreateClipCommand::performAction() {
    auto& clipManager = ClipManager::getInstance();

    if (type_ == ClipType::Audio) {
        createdClipId_ =
            clipManager.createAudioClip(trackId_, startTime_, length_, audioFilePath_, view_);
    } else {
        createdClipId_ = clipManager.createMidiClip(trackId_, startTime_, length_, view_);
    }
}

bool CreateClipCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    // If clip was created, validate it exists and has valid track
    if (createdClipId_ != INVALID_CLIP_ID) {
        auto* clip = clipManager.getClip(createdClipId_);
        if (!clip) {
            std::cerr << "ERROR: Created clip " << createdClipId_ << " does not exist!"
                      << std::endl;
            return false;
        }

        // Validate clip has valid track
        if (clip->trackId == INVALID_TRACK_ID) {
            std::cerr << "ERROR: Created clip " << createdClipId_ << " has invalid track!"
                      << std::endl;
            return false;
        }
    }

    return true;
}

// ============================================================================
// DuplicateClipCommand
// ============================================================================

DuplicateClipCommand::DuplicateClipCommand(ClipId sourceClipId, double startTime,
                                           TrackId targetTrackId)
    : sourceClipId_(sourceClipId), startTime_(startTime), targetTrackId_(targetTrackId) {}

bool DuplicateClipCommand::canExecute() const {
    return ClipManager::getInstance().getClip(sourceClipId_) != nullptr;
}

DuplicateClipState DuplicateClipCommand::captureState() {
    DuplicateClipState state;
    state.duplicatedClipId = duplicatedClipId_;
    state.wasDuplicated = (duplicatedClipId_ != INVALID_CLIP_ID);
    return state;
}

void DuplicateClipCommand::restoreState(const DuplicateClipState& state) {
    auto& clipManager = ClipManager::getInstance();

    // If restoring to state where clip didn't exist, delete current duplicate
    if (!state.wasDuplicated && duplicatedClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(duplicatedClipId_);
        duplicatedClipId_ = INVALID_CLIP_ID;
    }
    // If restoring to state where it did exist, recreate it (redo)
    else if (state.wasDuplicated && state.duplicatedClipId != INVALID_CLIP_ID &&
             duplicatedClipId_ == INVALID_CLIP_ID) {
        performAction();
    }
}

void DuplicateClipCommand::performAction() {
    auto& clipManager = ClipManager::getInstance();

    if (startTime_ < 0) {
        duplicatedClipId_ = clipManager.duplicateClip(sourceClipId_);
    } else {
        duplicatedClipId_ = clipManager.duplicateClipAt(sourceClipId_, startTime_, targetTrackId_);
    }
}

bool DuplicateClipCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    // If clip was created, validate it exists and has valid track
    if (duplicatedClipId_ != INVALID_CLIP_ID) {
        auto* clip = clipManager.getClip(duplicatedClipId_);
        if (!clip) {
            std::cerr << "ERROR: Duplicated clip " << duplicatedClipId_ << " does not exist!"
                      << std::endl;
            return false;
        }

        // Validate clip has valid track
        if (clip->trackId == INVALID_TRACK_ID) {
            std::cerr << "ERROR: Duplicated clip " << duplicatedClipId_ << " has invalid track!"
                      << std::endl;
            return false;
        }
    }

    return true;
}

// ============================================================================
// PasteClipCommand Implementation
// ============================================================================

PasteClipCommand::PasteClipCommand(double pasteTime, TrackId targetTrackId)
    : pasteTime_(pasteTime), targetTrackId_(targetTrackId) {}

bool PasteClipCommand::canExecute() const {
    return ClipManager::getInstance().hasClipsInClipboard();
}

PasteClipState PasteClipCommand::captureState() {
    PasteClipState state;
    state.pastedClipIds = pastedClipIds_;
    state.wasPasted = !pastedClipIds_.empty();
    return state;
}

void PasteClipCommand::restoreState(const PasteClipState& state) {
    auto& clipManager = ClipManager::getInstance();

    // If restoring to state where clips didn't exist, delete all pasted clips
    if (!state.wasPasted && !pastedClipIds_.empty()) {
        for (ClipId clipId : pastedClipIds_) {
            clipManager.deleteClip(clipId);
        }
        pastedClipIds_.clear();
    }
    // If restoring to state where clips existed, recreate them (redo)
    else if (state.wasPasted && !state.pastedClipIds.empty() && pastedClipIds_.empty()) {
        performAction();
    }
}

void PasteClipCommand::performAction() {
    auto& clipManager = ClipManager::getInstance();
    pastedClipIds_ = clipManager.pasteFromClipboard(pasteTime_, targetTrackId_);
}

bool PasteClipCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    // If clips were created, validate they all exist and have valid tracks
    for (ClipId clipId : pastedClipIds_) {
        auto* clip = clipManager.getClip(clipId);
        if (!clip) {
            std::cerr << "ERROR: Pasted clip " << clipId << " does not exist!" << std::endl;
            return false;
        }

        // Validate clip has valid track
        if (clip->trackId == INVALID_TRACK_ID) {
            std::cerr << "ERROR: Pasted clip " << clipId << " has invalid track!" << std::endl;
            return false;
        }
    }

    return true;
}

}  // namespace magda
