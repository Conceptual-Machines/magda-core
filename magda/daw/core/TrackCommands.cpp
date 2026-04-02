#include "TrackCommands.hpp"

#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "ClipManager.hpp"

namespace magda {

// ============================================================================
// CreateTrackCommand
// ============================================================================

CreateTrackCommand::CreateTrackCommand(TrackType type, const juce::String& name,
                                       TrackId afterTrackId)
    : type_(type), name_(name), afterTrackId_(afterTrackId) {}

void CreateTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    if (type_ == TrackType::Group) {
        createdTrackId_ = trackManager.createGroupTrack(name_);
    } else {
        createdTrackId_ = trackManager.createTrack(name_, type_);
    }

    // Move next to the specified track if provided
    if (afterTrackId_ != INVALID_TRACK_ID && createdTrackId_ != INVALID_TRACK_ID) {
        int afterIndex = trackManager.getTrackIndex(afterTrackId_);
        if (afterIndex >= 0) {
            trackManager.moveTrack(createdTrackId_, afterIndex + 1);
        }
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
            return "Create Track";
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

// ============================================================================
// AddDeviceToTrackCommand
// ============================================================================

AddDeviceToTrackCommand::AddDeviceToTrackCommand(TrackId trackId, const DeviceInfo& device)
    : trackId_(trackId), device_(device) {}

void AddDeviceToTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();
    createdDeviceId_ = trackManager.addDeviceToTrack(trackId_, device_);
    executed_ = (createdDeviceId_ != INVALID_DEVICE_ID);
    DBG("UNDO: Added device to track " << trackId_ << " (deviceId=" << createdDeviceId_ << ")");
}

void AddDeviceToTrackCommand::undo() {
    if (!executed_ || createdDeviceId_ == INVALID_DEVICE_ID) {
        return;
    }

    TrackManager::getInstance().removeDeviceFromTrack(trackId_, createdDeviceId_);
    DBG("UNDO: Removed device " << createdDeviceId_ << " from track " << trackId_);
}

// ============================================================================
// RemoveDeviceFromTrackCommand
// ============================================================================

RemoveDeviceFromTrackCommand::RemoveDeviceFromTrackCommand(TrackId trackId, DeviceId deviceId)
    : trackId_(trackId), deviceId_(deviceId) {}

void RemoveDeviceFromTrackCommand::execute() {
    auto& tm = TrackManager::getInstance();

    // Flush the plugin's live state into DeviceInfo before capturing
    if (auto* engine = tm.getAudioEngine()) {
        if (auto* bridge = engine->getAudioBridge()) {
            DBG("UNDO: Capturing plugin state for device " << deviceId_);
            bridge->getPluginManager().capturePluginState(deviceId_);
        } else {
            DBG("UNDO: WARNING - no AudioBridge, cannot capture plugin state");
        }
    } else {
        DBG("UNDO: WARNING - no AudioEngine, cannot capture plugin state");
    }

    // Save the device info and position before removing
    const auto& elements = tm.getChainElements(trackId_);
    for (int i = 0; i < static_cast<int>(elements.size()); ++i) {
        if (isDevice(elements[i]) && getDevice(elements[i]).id == deviceId_) {
            savedDevice_ = getDevice(elements[i]);
            savedIndex_ = i;
            break;
        }
    }

    if (savedIndex_ < 0)
        return;

    DBG("UNDO: Captured device state, pluginState length=" << savedDevice_.pluginState.length());

    tm.removeDeviceFromTrack(trackId_, deviceId_);
    executed_ = true;
    DBG("UNDO: Removed device " << savedDevice_.name << " (id=" << deviceId_ << ") from track "
                                << trackId_ << " at index " << savedIndex_);
}

void RemoveDeviceFromTrackCommand::undo() {
    if (!executed_)
        return;

    DBG("UNDO: Restoring device " << savedDevice_.name << " (id=" << deviceId_
                                  << "), pluginState length=" << savedDevice_.pluginState.length());
    auto& tm = TrackManager::getInstance();
    tm.addDeviceToTrack(trackId_, savedDevice_, savedIndex_);
    DBG("UNDO: Restored device " << savedDevice_.name << " (id=" << deviceId_ << ") to track "
                                 << trackId_ << " at index " << savedIndex_);
}

// ============================================================================
// CreateTrackWithDeviceCommand
// ============================================================================

CreateTrackWithDeviceCommand::CreateTrackWithDeviceCommand(const juce::String& trackName,
                                                           TrackType type, const DeviceInfo& device)
    : trackName_(trackName), type_(type), device_(device) {}

void CreateTrackWithDeviceCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    createdTrackId_ = trackManager.createTrack(trackName_, type_);
    if (createdTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    createdDeviceId_ = trackManager.addDeviceToTrack(createdTrackId_, device_);
    trackManager.setSelectedTrack(createdTrackId_);

    executed_ = true;
    DBG("UNDO: Created track " << createdTrackId_ << " with device " << createdDeviceId_);
}

void CreateTrackWithDeviceCommand::undo() {
    if (!executed_ || createdTrackId_ == INVALID_TRACK_ID) {
        return;
    }

    // Remove the device first
    if (createdDeviceId_ != INVALID_DEVICE_ID) {
        TrackManager::getInstance().removeDeviceFromTrack(createdTrackId_, createdDeviceId_);
    }

    // Delete all clips on this track before deleting the track
    auto& clipManager = ClipManager::getInstance();
    auto clipIds = clipManager.getClipsOnTrack(createdTrackId_);
    for (auto clipId : clipIds) {
        clipManager.deleteClip(clipId);
    }

    TrackManager::getInstance().deleteTrack(createdTrackId_);
    DBG("UNDO: Undid create track with device " << createdTrackId_);
}

}  // namespace magda
