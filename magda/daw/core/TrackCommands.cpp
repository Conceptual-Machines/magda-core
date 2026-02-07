#include "TrackCommands.hpp"

#include "ClipManager.hpp"

namespace magda {

// ============================================================================
// CreateTrackCommand
// ============================================================================

CreateTrackCommand::CreateTrackCommand(TrackType type) : type_(type) {}

void CreateTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    if (type_ == TrackType::Group) {
        createdTrackId_ = trackManager.createGroupTrack();
    } else {
        createdTrackId_ = trackManager.createTrack("", type_);
    }

    executed_ = true;
    DBG("UNDO: Created track " << createdTrackId_);
}

void CreateTrackCommand::undo() {
    if (!executed_ || createdTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    // Delete all clips on this track before deleting the track
    auto& clipManager = ClipManager::getInstance();
    auto clipIds = clipManager.getClipsOnTrack(createdTrackId_);
    for (auto clipId : clipIds) {
        clipManager.deleteClip(clipId);
    }

    TrackManager::getInstance().deleteTrack(createdTrackId_);
    DBG("UNDO: Undid create track " << createdTrackId_);
}

juce::String CreateTrackCommand::getDescription() const {
    switch (type_) {
        case TrackType::Audio:
            return "Create Audio Track";
        case TrackType::MIDI:
            return "Create MIDI Track";
        case TrackType::Group:
            return "Create Group Track";
        case TrackType::Aux:
            return "Create Aux Track";
        case TrackType::Master:
            return "Create Master Track";
        default:
            return "Create Track";
    }
}

// ============================================================================
// DeleteTrackCommand
// ============================================================================

DeleteTrackCommand::DeleteTrackCommand(TrackId trackId) : trackId_(trackId) {}

void DeleteTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();
    const auto* track = trackManager.getTrack(trackId_);

    if (!track) {
        return;
    }

    // Store full track info and clips for undo (only on first execute)
    if (!executed_) {
        storedTrack_ = *track;
    }

    // Store and remove all clips on this track
    auto& clipManager = ClipManager::getInstance();
    auto clipIds = clipManager.getClipsOnTrack(trackId_);
    storedClips_.clear();
    for (auto clipId : clipIds) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip) {
            storedClips_.push_back(*clip);
        }
        clipManager.deleteClip(clipId);
    }

    trackManager.deleteTrack(trackId_);
    executed_ = true;

    DBG("UNDO: Deleted track " << trackId_);
}

void DeleteTrackCommand::undo() {
    if (!executed_) {
        return;
    }

    TrackManager::getInstance().restoreTrack(storedTrack_);

    // Restore clips that were on this track
    auto& clipManager = ClipManager::getInstance();
    for (const auto& clip : storedClips_) {
        clipManager.restoreClip(clip);
    }

    DBG("UNDO: Restored track " << trackId_);
}

// ============================================================================
// DuplicateTrackCommand
// ============================================================================

DuplicateTrackCommand::DuplicateTrackCommand(TrackId sourceTrackId, bool duplicateContent)
    : sourceTrackId_(sourceTrackId), duplicateContent_(duplicateContent) {}

void DuplicateTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    duplicatedTrackId_ = trackManager.duplicateTrack(sourceTrackId_);

    if (duplicateContent_ && duplicatedTrackId_ != INVALID_TRACK_ID) {
        auto& clipManager = ClipManager::getInstance();
        auto clipIds = clipManager.getClipsOnTrack(sourceTrackId_);
        for (auto clipId : clipIds) {
            const auto* clip = clipManager.getClip(clipId);
            if (clip) {
                clipManager.duplicateClipAt(clipId, clip->startTime, duplicatedTrackId_);
            }
        }
    }

    executed_ = true;
    DBG("UNDO: Duplicated track " << sourceTrackId_ << " -> " << duplicatedTrackId_);
}

void DuplicateTrackCommand::undo() {
    if (!executed_ || duplicatedTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    // Delete all clips on the duplicated track before deleting the track
    auto& clipManager = ClipManager::getInstance();
    auto clipIds = clipManager.getClipsOnTrack(duplicatedTrackId_);
    for (auto clipId : clipIds) {
        clipManager.deleteClip(clipId);
    }

    TrackManager::getInstance().deleteTrack(duplicatedTrackId_);
    DBG("UNDO: Undid duplicate track " << duplicatedTrackId_);
}

}  // namespace magda
