#include "TrackCommands.hpp"

#include <iostream>

namespace magica {

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
    std::cout << "📝 UNDO: Created track " << createdTrackId_ << std::endl;
}

void CreateTrackCommand::undo() {
    if (!executed_ || createdTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    TrackManager::getInstance().deleteTrack(createdTrackId_);
    std::cout << "📝 UNDO: Undid create track " << createdTrackId_ << std::endl;
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

    // Store full track info for undo (only on first execute)
    if (!executed_) {
        storedTrack_ = *track;
    }

    trackManager.deleteTrack(trackId_);
    executed_ = true;

    std::cout << "📝 UNDO: Deleted track " << trackId_ << std::endl;
}

void DeleteTrackCommand::undo() {
    if (!executed_) {
        return;
    }

    TrackManager::getInstance().restoreTrack(storedTrack_);
    std::cout << "📝 UNDO: Restored track " << trackId_ << std::endl;
}

// ============================================================================
// DuplicateTrackCommand
// ============================================================================

DuplicateTrackCommand::DuplicateTrackCommand(TrackId sourceTrackId)
    : sourceTrackId_(sourceTrackId) {}

void DuplicateTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    // Get the current track count to determine the new track's ID
    int trackCountBefore = trackManager.getNumTracks();

    trackManager.duplicateTrack(sourceTrackId_);

    // Find the newly created track (it should be the last one added)
    const auto& tracks = trackManager.getTracks();
    if (static_cast<int>(tracks.size()) > trackCountBefore) {
        // Find the track that was just added (the duplicate)
        for (const auto& track : tracks) {
            if (track.name.endsWith(" Copy")) {
                duplicatedTrackId_ = track.id;
                break;
            }
        }
    }

    executed_ = true;
    std::cout << "📝 UNDO: Duplicated track " << sourceTrackId_ << " -> " << duplicatedTrackId_
              << std::endl;
}

void DuplicateTrackCommand::undo() {
    if (!executed_ || duplicatedTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    TrackManager::getInstance().deleteTrack(duplicatedTrackId_);
    std::cout << "📝 UNDO: Undid duplicate track " << duplicatedTrackId_ << std::endl;
}

}  // namespace magica
