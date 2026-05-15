#include "TrackCommands.hpp"

#include <limits>

#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "../project/ProjectManager.hpp"
#include "ClipManager.hpp"
#include "TempoUtils.hpp"

namespace magda {

namespace {
ChainNodePath getChainElementParentPath(const ChainNodePath& path) {
    ChainNodePath parent;
    parent.trackId = path.trackId;
    if (path.topLevelDeviceId != INVALID_DEVICE_ID)
        return parent;

    parent.steps = path.steps;
    if (!parent.steps.empty())
        parent.steps.pop_back();
    return parent;
}

std::vector<ChainElement> deepCopyChainElements(const std::vector<ChainElement>& elements) {
    std::vector<ChainElement> copied;
    copied.reserve(elements.size());
    for (const auto& element : elements)
        copied.push_back(deepCopyElement(element));
    return copied;
}

ChainNodePath findChainElementPathRecursive(const ChainNodePath& parentPath,
                                            const std::vector<ChainElement>& elements,
                                            ChainStepType type, int id) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (type == ChainStepType::Device && device.id == id) {
                if (parentPath.steps.empty())
                    return ChainNodePath::topLevelDevice(parentPath.trackId, device.id);
                return parentPath.withDevice(device.id);
            }
            continue;
        }

        const auto& rack = getRack(element);
        auto rackPath = parentPath.withRack(rack.id);
        if (type == ChainStepType::Rack && rack.id == id)
            return rackPath;

        for (const auto& chain : rack.chains) {
            if (auto path = findChainElementPathRecursive(rackPath.withChain(chain.id),
                                                          chain.elements, type, id);
                path.isValid())
                return path;
        }
    }

    return {};
}

ChainNodePath findChainElementPath(TrackManager& tm, ChainStepType type, int id) {
    for (const auto& track : tm.getTracks()) {
        ChainNodePath trackPath;
        trackPath.trackId = track.id;
        if (auto path = findChainElementPathRecursive(trackPath, track.chainElements, type, id);
            path.isValid())
            return path;
    }

    if (const auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
        ChainNodePath masterPath;
        masterPath.trackId = MASTER_TRACK_ID;
        if (auto path =
                findChainElementPathRecursive(masterPath, masterTrack->chainElements, type, id);
            path.isValid())
            return path;
    }

    return {};
}

bool describeChainElementPath(const ChainNodePath& path, ChainStepType& type, int& id) {
    if (path.topLevelDeviceId != INVALID_DEVICE_ID) {
        type = ChainStepType::Device;
        id = path.topLevelDeviceId;
        return true;
    }

    if (!path.steps.empty() && (path.steps.back().type == ChainStepType::Device ||
                                path.steps.back().type == ChainStepType::Rack)) {
        type = path.steps.back().type;
        id = path.steps.back().id;
        return true;
    }

    return false;
}
}  // namespace

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

DuplicateTrackCommand::DuplicateTrackCommand(TrackId sourceTrackId, bool duplicateContent,
                                             bool duplicateDevices)
    : sourceTrackId_(sourceTrackId),
      duplicateContent_(duplicateContent),
      duplicateDevices_(duplicateDevices) {}

void DuplicateTrackCommand::execute() {
    auto& trackManager = TrackManager::getInstance();

    // Capture current plugin state so the duplicate gets the source's live settings.
    // Skipped when we're stripping the FX chain anyway — nothing to carry over.
    if (duplicateDevices_) {
        if (auto* engine = trackManager.getAudioEngine()) {
            if (auto* bridge = engine->getAudioBridge()) {
                bridge->captureAllPluginStates();
            }
        }
    }

    duplicatedTrackId_ = trackManager.duplicateTrack(sourceTrackId_, duplicateDevices_);

    if (duplicateContent_ && duplicatedTrackId_ != INVALID_TRACK_ID) {
        auto& clipManager = ClipManager::getInstance();
        auto clipIds = clipManager.getClipsOnTrack(sourceTrackId_);
        const double projectBpm = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        const double bpm = isValidBpm(projectBpm) ? projectBpm : DEFAULT_BPM;
        for (auto clipId : clipIds) {
            const auto* clip = clipManager.getClip(clipId);
            if (clip) {
                clipManager.duplicateClipAt(clipId, clip->getTimelineStart(bpm), duplicatedTrackId_,
                                            bpm);
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
// MoveChainElementCommand
// ============================================================================

MoveChainElementCommand::MoveChainElementCommand(const ChainNodePath& sourceElementPath,
                                                 const ChainNodePath& destinationChainPath,
                                                 int insertIndex)
    : sourceElementPath_(sourceElementPath),
      destinationChainPath_(destinationChainPath),
      insertIndex_(insertIndex) {}

ChainNodePath MoveChainElementCommand::buildMovedPath(
    const ChainNodePath& destinationChainPath) const {
    if (sourceType_ == ChainStepType::Device) {
        if (destinationChainPath.steps.empty())
            return ChainNodePath::topLevelDevice(destinationChainPath.trackId, sourceId_);
        return destinationChainPath.withDevice(sourceId_);
    }

    return destinationChainPath.withRack(sourceId_);
}

void MoveChainElementCommand::execute() {
    auto& tm = TrackManager::getInstance();

    if (sourceElementPath_.topLevelDeviceId != INVALID_DEVICE_ID) {
        sourceType_ = ChainStepType::Device;
        sourceId_ = sourceElementPath_.topLevelDeviceId;
        undoChainPath_ = {};
        undoChainPath_.trackId = sourceElementPath_.trackId;
    } else if (!sourceElementPath_.steps.empty() &&
               (sourceElementPath_.steps.back().type == ChainStepType::Device ||
                sourceElementPath_.steps.back().type == ChainStepType::Rack)) {
        sourceType_ = sourceElementPath_.steps.back().type;
        sourceId_ = sourceElementPath_.steps.back().id;
        undoChainPath_ = sourceElementPath_;
        undoChainPath_.steps.pop_back();
    } else {
        executed_ = false;
        return;
    }

    undoIndex_ = tm.getChainElementIndex(sourceElementPath_);
    if (undoIndex_ < 0) {
        executed_ = false;
        return;
    }

    executed_ = tm.moveChainElement(sourceElementPath_, destinationChainPath_, insertIndex_);
    if (executed_)
        movedElementPath_ = buildMovedPath(destinationChainPath_);
}

void MoveChainElementCommand::undo() {
    if (!executed_)
        return;

    TrackManager::getInstance().moveChainElement(movedElementPath_, undoChainPath_, undoIndex_);
}

// ============================================================================
// MoveChainElementsCommand
// ============================================================================

MoveChainElementsCommand::MoveChainElementsCommand(std::vector<ChainNodePath> sourceElementPaths,
                                                   const ChainNodePath& destinationChainPath,
                                                   int insertIndex)
    : sourceElementPaths_(std::move(sourceElementPaths)),
      destinationChainPath_(destinationChainPath),
      insertIndex_(insertIndex) {}

void MoveChainElementsCommand::execute() {
    auto& tm = TrackManager::getInstance();
    std::vector<MovedElementRecord> records;
    records.reserve(sourceElementPaths_.size());

    for (const auto& path : sourceElementPaths_) {
        if (!path.isValid())
            continue;

        ChainStepType type = ChainStepType::Device;
        int id = INVALID_DEVICE_ID;
        if (!describeChainElementPath(path, type, id))
            continue;

        const auto parentPath = getChainElementParentPath(path);
        const int index = tm.getChainElementIndex(path);
        if (index < 0)
            continue;

        const bool alreadyRecorded =
            std::any_of(records.begin(), records.end(), [type, id](const auto& record) {
                return record.type == type && record.id == id;
            });
        if (alreadyRecorded)
            continue;

        records.push_back({path, parentPath, index, type, id});
    }

    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        const auto& parentA = a.originalParentPath;
        const auto& parentB = b.originalParentPath;
        if (parentA == parentB)
            return a.originalIndex < b.originalIndex;
        if (parentA.trackId != parentB.trackId)
            return parentA.trackId < parentB.trackId;
        return parentA.toString() < parentB.toString();
    });

    commands_.clear();
    commands_.reserve(records.size());
    movedElements_.clear();
    movedElements_.reserve(records.size());
    executed_ = false;

    int offset = 0;
    for (const auto& record : records) {
        auto command = std::make_unique<MoveChainElementCommand>(
            record.originalPath, destinationChainPath_, insertIndex_ + offset);
        command->execute();
        if (command->didMove()) {
            executed_ = true;
            ++offset;
            movedElements_.push_back(record);
            commands_.push_back(std::move(command));
        }
    }
}

void MoveChainElementsCommand::undo() {
    if (!executed_)
        return;

    auto& tm = TrackManager::getInstance();
    auto records = movedElements_;
    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        if (a.originalParentPath == b.originalParentPath)
            return a.originalIndex < b.originalIndex;
        if (a.originalParentPath.trackId != b.originalParentPath.trackId)
            return a.originalParentPath.trackId < b.originalParentPath.trackId;
        return a.originalParentPath.toString() < b.originalParentPath.toString();
    });

    auto restoreRecord = [&tm](const auto& record) {
        auto currentPath = findChainElementPath(tm, record.type, record.id);
        if (!currentPath.isValid())
            return;

        int insertIndex = record.originalIndex;
        const auto currentParentPath = getChainElementParentPath(currentPath);
        const int currentIndex = tm.getChainElementIndex(currentPath);
        if (currentParentPath == record.originalParentPath) {
            if (currentIndex == record.originalIndex)
                return;
            if (currentIndex >= 0 && currentIndex < record.originalIndex)
                ++insertIndex;
        }

        tm.moveChainElement(currentPath, record.originalParentPath, insertIndex);
    };

    auto begin = records.begin();
    while (begin != records.end()) {
        auto end = std::find_if(begin, records.end(), [&](const auto& record) {
            return record.originalParentPath != begin->originalParentPath;
        });

        const auto minOriginalIt = std::min_element(begin, end, [](const auto& a, const auto& b) {
            return a.originalIndex < b.originalIndex;
        });
        int minCurrentIndex = std::numeric_limits<int>::max();
        bool allStillInOriginalContainer = true;

        for (auto it = begin; it != end; ++it) {
            const auto currentPath = findChainElementPath(tm, it->type, it->id);
            if (!currentPath.isValid() ||
                getChainElementParentPath(currentPath) != it->originalParentPath) {
                allStillInOriginalContainer = false;
                break;
            }

            const int currentIndex = tm.getChainElementIndex(currentPath);
            if (currentIndex >= 0)
                minCurrentIndex = std::min(minCurrentIndex, currentIndex);
        }

        if (allStillInOriginalContainer && minOriginalIt != end &&
            minCurrentIndex < minOriginalIt->originalIndex) {
            for (auto it = std::make_reverse_iterator(end); it != std::make_reverse_iterator(begin);
                 ++it)
                restoreRecord(*it);
        } else {
            for (auto it = begin; it != end; ++it)
                restoreRecord(*it);
        }

        begin = end;
    }
}

// ============================================================================
// PasteChainElementsCommand
// ============================================================================

PasteChainElementsCommand::PasteChainElementsCommand(const ChainNodePath& destinationChainPath,
                                                     std::vector<ChainElement> elements,
                                                     int insertIndex)
    : destinationChainPath_(destinationChainPath),
      templateElements_(std::move(elements)),
      insertIndex_(insertIndex) {}

void PasteChainElementsCommand::execute() {
    auto& tm = TrackManager::getInstance();
    auto elements = deepCopyChainElements(templateElements_);
    const int requestedIndex = insertIndex_;
    executed_ = tm.insertChainElementsByPath(destinationChainPath_, std::move(elements),
                                             requestedIndex, true);
    insertedPaths_.clear();
    if (!executed_)
        return;

    const auto* track = tm.getTrack(destinationChainPath_.trackId);
    if (!track)
        return;

    const auto& destinationElements =
        destinationChainPath_.steps.empty()
            ? track->chainElements
            : tm.getChain(destinationChainPath_.trackId, destinationChainPath_.getRackId(),
                          destinationChainPath_.getChainId())
                  ->elements;
    const int start = std::clamp(requestedIndex, 0, static_cast<int>(destinationElements.size()));
    for (int i = 0; i < static_cast<int>(templateElements_.size()) &&
                    start + i < static_cast<int>(destinationElements.size());
         ++i) {
        const auto& element = destinationElements[static_cast<size_t>(start + i)];
        if (isDevice(element)) {
            const auto deviceId = getDevice(element).id;
            insertedPaths_.push_back(
                destinationChainPath_.steps.empty()
                    ? ChainNodePath::topLevelDevice(destinationChainPath_.trackId, deviceId)
                    : destinationChainPath_.withDevice(deviceId));
        } else if (isRack(element)) {
            insertedPaths_.push_back(destinationChainPath_.withRack(getRack(element).id));
        }
    }
}

void PasteChainElementsCommand::undo() {
    auto& tm = TrackManager::getInstance();
    for (auto it = insertedPaths_.rbegin(); it != insertedPaths_.rend(); ++it) {
        if (it->getType() == ChainNodeType::TopLevelDevice ||
            it->getType() == ChainNodeType::Device)
            tm.removeDeviceFromChainByPath(*it);
        else if (it->getType() == ChainNodeType::Rack)
            tm.removeRackFromChainByPath(*it);
    }
}

// ============================================================================
// WrapChainElementsInRackCommand
// ============================================================================

WrapChainElementsInRackCommand::WrapChainElementsInRackCommand(
    std::vector<ChainNodePath> sourceElementPaths, juce::String rackName)
    : sourceElementPaths_(std::move(sourceElementPaths)), rackName_(std::move(rackName)) {}

void WrapChainElementsInRackCommand::execute() {
    auto& tm = TrackManager::getInstance();
    if (sourceElementPaths_.empty()) {
        executed_ = false;
        return;
    }

    sourceChainPath_ = getChainElementParentPath(sourceElementPaths_.front());
    sourceIndex_ = std::numeric_limits<int>::max();
    for (const auto& path : sourceElementPaths_) {
        if (getChainElementParentPath(path) != sourceChainPath_) {
            executed_ = false;
            return;
        }

        const int index = tm.getChainElementIndex(path);
        if (index >= 0)
            sourceIndex_ = std::min(sourceIndex_, index);
    }
    if (sourceIndex_ == std::numeric_limits<int>::max()) {
        executed_ = false;
        return;
    }

    rackId_ = tm.wrapChainElementsInRack(sourceElementPaths_, rackName_);
    executed_ = rackId_ != INVALID_RACK_ID;

    if (executed_) {
        auto rackPath = sourceChainPath_.withRack(rackId_);
        if (auto* rack = tm.getRackByPath(rackPath); rack != nullptr && !rack->chains.empty())
            chainId_ = rack->chains.front().id;
    }
}

void WrapChainElementsInRackCommand::undo() {
    if (!executed_ || rackId_ == INVALID_RACK_ID || chainId_ == INVALID_CHAIN_ID)
        return;

    auto& tm = TrackManager::getInstance();
    auto rackPath = sourceChainPath_.withRack(rackId_);
    auto* rack = tm.getRackByPath(rackPath);
    if (!rack || rack->chains.empty())
        return;

    auto chainPath = rackPath.withChain(chainId_);
    std::vector<ChainNodePath> childPaths;
    for (const auto& element : rack->chains.front().elements) {
        if (isDevice(element))
            childPaths.push_back(chainPath.withDevice(getDevice(element).id));
        else if (isRack(element))
            childPaths.push_back(chainPath.withRack(getRack(element).id));
    }

    int insertIndex = sourceIndex_;
    for (const auto& childPath : childPaths)
        tm.moveChainElement(childPath, sourceChainPath_, insertIndex++);

    tm.removeRackFromChainByPath(rackPath);
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
