#include "TrackCommands.hpp"

#include <algorithm>
#include <functional>
#include <limits>

#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "../project/ProjectManager.hpp"
#include "ClipManager.hpp"
#include "TempoUtils.hpp"

namespace magda {

namespace {
ChainNodePath parentChainOf(const ChainNodePath& path) {
    return path.parentChain();
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
        if (auto path =
                findChainElementPathRecursive(trackPath, track.chain.fxChainElements, type, id);
            path.isValid())
            return path;
    }

    if (const auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
        ChainNodePath masterPath;
        masterPath.trackId = MASTER_TRACK_ID;
        if (auto path = findChainElementPathRecursive(masterPath,
                                                      masterTrack->chain.fxChainElements, type, id);
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

/// The index to hand `moveChainElement()` so @p elementPath comes to rest at
/// @p homeIndex of @p homeChain.
///
/// The two are not the same number. `homeIndex` is where the element stood,
/// counted with itself still in the list. `moveChainElement()` takes a drop
/// position -- insert before whatever stands there now -- and drops one when
/// the element is travelling up a list it is already in, because removing it
/// first shifts everything past it down. So an element on its way back up its
/// own container has to aim one slot past its home to land on it.
///
/// One conversion, because it was written twice and only one of them was right:
/// the multi-element undo made the correction, the single-element one passed the
/// home index straight through, and so undoing a drag that moved a device
/// towards the front of its chain put it back one slot too early
/// (#2229). `WrapChainElementsInRackCommand::undo()` makes the same correction
/// against the standing rack.
int dropIndexForHome(TrackManager& tm, const ChainNodePath& elementPath,
                     const ChainNodePath& homeChain, int homeIndex) {
    if (parentChainOf(elementPath) != homeChain)
        return homeIndex;

    const int currentIndex = tm.getChainElementIndex(elementPath);
    return currentIndex >= 0 && currentIndex < homeIndex ? homeIndex + 1 : homeIndex;
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
        case TrackType::Media:
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

std::vector<DeleteTrackCommand::DeletedTrack> DeleteTrackCommand::collectSubtree(TrackId trackId) {
    auto& tm = TrackManager::getInstance();
    std::vector<DeletedTrack> records;

    const std::function<void(TrackId)> descend = [&](TrackId id) {
        const auto* track = tm.getTrack(id);
        if (track == nullptr)
            return;

        DeletedTrack record;
        record.track = *track;
        record.position = tm.restorePositionOf(id);

        auto& clipManager = ClipManager::getInstance();
        for (auto clipId : clipManager.getClipsOnTrack(id))
            if (const auto* clip = clipManager.getClip(clipId))
                record.clips.push_back(*clip);

        records.push_back(std::move(record));

        for (auto childId : track->childIds)
            descend(childId);
    };

    descend(trackId);

    // By where each stood, so refilling the list left to right lands every one
    // of them on its own index.
    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.position.trackIndex < b.position.trackIndex;
    });
    return records;
}

void DeleteTrackCommand::execute() {
    // The master track is permanent. Bail before touching clips or storing undo
    // state so the command is a clean no-op (undo() is gated on executed_).
    if (trackId_ == MASTER_TRACK_ID) {
        return;
    }

    auto& trackManager = TrackManager::getInstance();
    const auto* track = trackManager.getTrack(trackId_);

    if (!track) {
        return;
    }

    // The whole subtree, because deleteTrack() cascades into the children and
    // takes their devices and clips with them (only on first execute).
    if (!executed_) {
        storedTracks_ = collectSubtree(trackId_);

        // And what it will clear on everything that outlives it.
        std::vector<TrackId> doomed;
        doomed.reserve(storedTracks_.size());
        for (const auto& record : storedTracks_)
            doomed.push_back(record.track.id);
        storedRouting_ = trackManager.externalRoutingInto(doomed);
    }

    // deleteTrack() deletes each track's clips as it goes, children included.
    trackManager.deleteTrack(trackId_);
    executed_ = true;

    DBG("UNDO: Deleted track " << trackId_ << " and " << (int)storedTracks_.size() - 1
                               << " descendant(s)");
}

void DeleteTrackCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();

    // Ascending by where each stood, so the list refills left to right. A child
    // restored before its parent finds no parent to rejoin, and needs none: the
    // parent's own stored childIds still names it.
    for (const auto& record : storedTracks_) {
        trackManager.restoreTrack(record.track, record.position);
        for (const auto& clip : record.clips)
            clipManager.restoreClip(clip);
    }

    // After the tracks are back, so a send, an input or a sidechain naming one
    // of them names something that exists again.
    trackManager.restoreExternalRouting(storedRouting_);

    DBG("UNDO: Restored track " << trackId_ << " and its descendants");
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
    auto& clipManager = ClipManager::getInstance();

    // A redo puts back what the first run made rather than duplicating again:
    // duplicating again allocates a fresh TrackId and fresh device, rack and
    // chain ids, so undo followed by redo would leave a different project than
    // the one the undo took away (#2229).
    if (executed_) {
        if (duplicatedTrackId_ == INVALID_TRACK_ID)
            return;
        trackManager.restoreTrack(storedTrack_, storedPosition_);
        for (const auto& clip : storedClips_)
            clipManager.restoreClip(clip);
        return;
    }

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

    // What it made, so a redo can make exactly that again.
    if (const auto* made = trackManager.getTrack(duplicatedTrackId_)) {
        storedTrack_ = *made;
        storedPosition_ = trackManager.restorePositionOf(duplicatedTrackId_);
        storedClips_.clear();
        for (auto clipId : clipManager.getClipsOnTrack(duplicatedTrackId_))
            if (const auto* clip = clipManager.getClip(clipId))
                storedClips_.push_back(*clip);
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
    const auto clipIds = clipManager.getClipsOnTrack(duplicatedTrackId_);
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
            bridge->getPluginManager().capturePluginState(
                ChainNodePath::topLevelDevice(trackId_, deviceId_));
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
    // Re-insert with ids preserved. addDeviceToTrack runs the device through
    // prepareNewDevice, which stamps a fresh DeviceId, so undo would restore it
    // under a different id and orphan every automation lane, macro link, and
    // alias that targeted it. ChainElement is move-only, hence the push_back.
    std::vector<ChainElement> elements;
    elements.push_back(makeDeviceElement(savedDevice_));
    tm.insertChainElementsByPath(ChainNodePath::topLevelDevice(trackId_, deviceId_).parentChain(),
                                 std::move(elements), savedIndex_, /*reassignIds=*/false);
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

    auto& tm = TrackManager::getInstance();
    tm.moveChainElement(movedElementPath_, undoChainPath_,
                        dropIndexForHome(tm, movedElementPath_, undoChainPath_, undoIndex_));
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

        const auto parentPath = parentChainOf(path);
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

        if (parentChainOf(currentPath) == record.originalParentPath &&
            tm.getChainElementIndex(currentPath) == record.originalIndex)
            return;

        tm.moveChainElement(
            currentPath, record.originalParentPath,
            dropIndexForHome(tm, currentPath, record.originalParentPath, record.originalIndex));
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
            if (!currentPath.isValid() || parentChainOf(currentPath) != it->originalParentPath) {
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
    const bool replaying = !materialised_.empty();

    // A redo replays what the first run produced, ids and all. Re-copying the
    // template would re-key it, giving every pasted device a fresh id and
    // orphaning the links, automation lanes and aliases made against the first
    // paste -- the same reason the removal commands restore under the id the
    // device had (#2221).
    auto elements =
        replaying ? deepCopyChainElements(materialised_) : deepCopyChainElements(templateElements_);
    const int elementCount = static_cast<int>(elements.size());

    // The index the insert will actually use, taken against the destination as
    // it is NOW. `insertChainElementsByPath()` clamps against the pre-insertion
    // size, so reading it back afterwards against the larger list would start
    // the bookkeeping past what was inserted: a stale or out-of-range index
    // recorded some of the pasted elements, or none, and undo then left them
    // behind while redo pasted another copy (#2221).
    const auto* destinationBefore =
        destinationChainPath_.steps.empty() ? nullptr : tm.getChainByPath(destinationChainPath_);
    const auto* trackBefore = tm.getTrack(destinationChainPath_.trackId);
    const int destinationSizeBefore =
        destinationChainPath_.steps.empty()
            ? (trackBefore != nullptr ? static_cast<int>(trackBefore->chain.fxChainElements.size())
                                      : 0)
            : (destinationBefore != nullptr ? static_cast<int>(destinationBefore->elements.size())
                                            : 0);
    const int requestedIndex = std::clamp(insertIndex_, 0, destinationSizeBefore);

    executed_ = tm.insertChainElementsByPath(destinationChainPath_, std::move(elements),
                                             requestedIndex, !replaying);
    insertedPaths_.clear();
    if (!executed_)
        return;

    const auto* track = tm.getTrack(destinationChainPath_.trackId);
    if (!track)
        return;

    // By path rather than by extracted ids: the destination can be nested to any
    // depth, and it can be a pad chain, whose owner step is a DeviceId that no
    // rack lookup answers to (#2219).
    const auto* destinationChain =
        destinationChainPath_.steps.empty() ? nullptr : tm.getChainByPath(destinationChainPath_);
    if (!destinationChainPath_.steps.empty() && destinationChain == nullptr)
        return;

    const auto& destinationElements = destinationChainPath_.steps.empty()
                                          ? track->chain.fxChainElements
                                          : destinationChain->elements;
    // `requestedIndex` is already the effective one, so the inserted elements are
    // exactly [start, start + elementCount).
    const int start = requestedIndex;
    for (int i = 0; i < elementCount && start + i < static_cast<int>(destinationElements.size());
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

        if (!replaying)
            materialised_.push_back(deepCopyElement(element));
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

    sourceChainPath_ = parentChainOf(sourceElementPaths_.front());
    sourceIndex_ = std::numeric_limits<int>::max();
    sourceIndices_.clear();
    for (const auto& path : sourceElementPaths_) {
        if (parentChainOf(path) != sourceChainPath_) {
            executed_ = false;
            return;
        }

        const int index = tm.getChainElementIndex(path);
        if (index >= 0) {
            sourceIndex_ = std::min(sourceIndex_, index);
            sourceIndices_.push_back(index);
        }
    }
    // Ascending, which is the order the wrap itself puts them in the rack, so an
    // index and a child line up.
    std::sort(sourceIndices_.begin(), sourceIndices_.end());
    if (sourceIndex_ == std::numeric_limits<int>::max()) {
        executed_ = false;
        return;
    }

    // A redo reuses the ids the first run allocated, so undo followed by redo
    // leaves the rack with the identity it had rather than a fresh one (#2221).
    const auto newRackId =
        tm.wrapChainElementsInRack(sourceElementPaths_, rackName_, rackId_, chainId_);
    rackId_ = newRackId;
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

    // Each child goes back to the index it came from, not to a run starting at
    // the lowest one: a selection can have gaps, and closing them reorders the
    // chain (#2221).
    //
    // The rack is still standing while they move, occupying one slot at
    // `rackIndex`, so a child whose home is past it aims one higher and lands
    // right when the rack goes. `rackIndex` rises as children are put in front
    // of it.
    int rackIndex = sourceIndex_;
    for (std::size_t i = 0; i < childPaths.size(); ++i) {
        const int home =
            i < sourceIndices_.size() ? sourceIndices_[i] : sourceIndex_ + static_cast<int>(i);
        const int target = home <= rackIndex ? home : home + 1;
        tm.moveChainElement(childPaths[i], sourceChainPath_, target);
        if (target <= rackIndex)
            ++rackIndex;
    }

    tm.removeRackFromChainByPath(rackPath);
}

// ============================================================================
// SetMacroNameCommand / SetModNameCommand
// ============================================================================

SetMacroNameCommand::SetMacroNameCommand(const ChainNodePath& path, int macroIndex,
                                         const juce::String& newName)
    : path_(path), macroIndex_(macroIndex), newName_(newName) {
    const auto& trackManager = TrackManager::getInstance();
    auto node = trackManager.resolveChainNode(path_);
    if (!node.valid() || node.macros == nullptr || macroIndex_ < 0 ||
        macroIndex_ >= static_cast<int>(node.macros->size()))
        return;

    oldName_ = (*node.macros)[static_cast<size_t>(macroIndex_)].name;
    valid_ = true;
}

void SetMacroNameCommand::execute() {
    applyName(newName_);
}

void SetMacroNameCommand::undo() {
    applyName(oldName_);
}

void SetMacroNameCommand::applyName(const juce::String& name) {
    if (!valid_)
        return;

    TrackManager::getInstance().setMacroName(path_, macroIndex_, name);
    TrackManager::getInstance().notifyModulationNamesChanged(path_.trackId);
}

SetModNameCommand::SetModNameCommand(const ChainNodePath& path, int modIndex,
                                     const juce::String& newName)
    : path_(path), modIndex_(modIndex), newName_(newName) {
    const auto& trackManager = TrackManager::getInstance();
    auto node = trackManager.resolveChainNode(path_);
    if (!node.valid() || node.mods == nullptr || modIndex_ < 0 ||
        modIndex_ >= static_cast<int>(node.mods->size()))
        return;

    oldName_ = (*node.mods)[static_cast<size_t>(modIndex_)].name;
    valid_ = true;
}

void SetModNameCommand::execute() {
    applyName(newName_);
}

void SetModNameCommand::undo() {
    applyName(oldName_);
}

void SetModNameCommand::applyName(const juce::String& name) {
    if (!valid_)
        return;

    TrackManager::getInstance().setModName(path_, modIndex_, name);
    TrackManager::getInstance().notifyModulationNamesChanged(path_.trackId);
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

    // createTrack() built the header before the device existed, and adding the
    // device only fires trackDevicesChanged (which does not re-run the
    // type/routing-dependent header setup). Rebuild now so the new track's
    // header is correct immediately instead of only after a view switch.
    trackManager.notifyTracksChanged();

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

// ============================================================================
// Path-based device commands
// ============================================================================

namespace {

void capturePluginStateAt(const ChainNodePath& devicePath) {
    auto& tm = TrackManager::getInstance();
    if (auto* engine = tm.getAudioEngine()) {
        if (auto* bridge = engine->getAudioBridge())
            bridge->getPluginManager().capturePluginState(devicePath);
    }
}

}  // namespace

AddDeviceByPathCommand::AddDeviceByPathCommand(const ChainNodePath& parentPath,
                                               const DeviceInfo& device, int insertIndex)
    : parentPath_(parentPath), device_(device), insertIndex_(insertIndex) {}

void AddDeviceByPathCommand::execute() {
    auto& tm = TrackManager::getInstance();

    if (parentPath_.getType() == ChainNodeType::Track) {
        createdDeviceId_ = insertIndex_ >= 0
                               ? tm.addDeviceToTrack(parentPath_.trackId, device_, insertIndex_)
                               : tm.addDeviceToTrack(parentPath_.trackId, device_);
        if (createdDeviceId_ != INVALID_DEVICE_ID)
            createdDevicePath_ =
                ChainNodePath::topLevelDevice(parentPath_.trackId, createdDeviceId_);
    } else {
        createdDeviceId_ = insertIndex_ >= 0
                               ? tm.addDeviceToChainByPath(parentPath_, device_, insertIndex_)
                               : tm.addDeviceToChainByPath(parentPath_, device_);
        if (createdDeviceId_ != INVALID_DEVICE_ID)
            createdDevicePath_ = parentPath_.withDevice(createdDeviceId_);
    }

    executed_ = createdDeviceId_ != INVALID_DEVICE_ID;
    DBG("UNDO: Added device by path " << parentPath_.toString() << " (deviceId=" << createdDeviceId_
                                      << ")");
}

void AddDeviceByPathCommand::undo() {
    if (!executed_ || !createdDevicePath_.isValid())
        return;
    TrackManager::getInstance().removeDeviceFromChainByPath(createdDevicePath_);
    DBG("UNDO: Removed added device " << createdDeviceId_);
}

RemoveDeviceByPathCommand::RemoveDeviceByPathCommand(const ChainNodePath& devicePath)
    : devicePath_(devicePath), parentPath_(devicePath.parentChain()) {}

void RemoveDeviceByPathCommand::execute() {
    auto& tm = TrackManager::getInstance();

    const auto* device = tm.getDeviceInChainByPath(devicePath_);
    if (device == nullptr)
        return;

    // Flush live plugin state into DeviceInfo so undo restores how it sounded.
    capturePluginStateAt(devicePath_);

    device = tm.getDeviceInChainByPath(devicePath_);
    if (device == nullptr)
        return;

    savedDevice_ = *device;
    savedIndex_ = tm.getChainElementIndex(devicePath_);
    if (savedIndex_ < 0)
        return;

    tm.removeDeviceFromChainByPath(devicePath_);
    executed_ = true;
    DBG("UNDO: Removed device " << savedDevice_.name << " at index " << savedIndex_);
}

void RemoveDeviceByPathCommand::undo() {
    if (!executed_)
        return;

    // Re-insert with ids preserved. The ordinary add path runs the device
    // through prepareNewDevice, which stamps a fresh DeviceId — undo would then
    // restore the device under a different id, leaving every automation lane,
    // macro link, and alias that targeted it pointing at nothing.
    // ChainElement holds a unique_ptr, so build the vector by move rather than
    // from an initializer list.
    std::vector<ChainElement> elements;
    elements.push_back(makeDeviceElement(savedDevice_));
    TrackManager::getInstance().insertChainElementsByPath(parentPath_, std::move(elements),
                                                          savedIndex_, /*reassignIds=*/false);

    DBG("UNDO: Restored device " << savedDevice_.name << " (id=" << savedDevice_.id << ") at index "
                                 << savedIndex_);
}

}  // namespace magda
