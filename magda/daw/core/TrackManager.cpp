#include "TrackManager.hpp"

#include <algorithm>
#include <map>

#include "../audio/AudioBridge.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/TracktionHelpers.hpp"
#include "../audio/plugins/SidechainTriggerBus.hpp"
#include "../engine/AudioEngine.hpp"
#include "ClipManager.hpp"
#include "Config.hpp"
#include "InternalDeviceKind.hpp"
#include "ModulatorEngine.hpp"
#include "PluginPreferences.hpp"
#include "RackInfo.hpp"
#include "SelectionManager.hpp"

namespace magda {

namespace {

struct DuplicateIdRemap {
    TrackId oldTrackId = INVALID_TRACK_ID;
    TrackId newTrackId = INVALID_TRACK_ID;
    std::map<DeviceId, DeviceId> devices;
    std::map<RackId, RackId> racks;
    std::map<ChainId, ChainId> chains;
};

template <typename Id> bool remapDuplicateId(const std::map<Id, Id>& ids, int& value) {
    auto it = ids.find(value);
    if (it == ids.end())
        return false;
    value = it->second;
    return true;
}

void remapDuplicatedPath(ChainNodePath& path, const DuplicateIdRemap& remap) {
    if (!path.isValid())
        return;

    bool touched = false;
    if (path.trackId == remap.oldTrackId) {
        path.trackId = remap.newTrackId;
        touched = true;
    }

    if (path.topLevelDeviceId != INVALID_DEVICE_ID)
        touched = remapDuplicateId(remap.devices, path.topLevelDeviceId) || touched;

    for (auto& step : path.steps) {
        switch (step.type) {
            case ChainStepType::Rack:
                touched = remapDuplicateId(remap.racks, step.id) || touched;
                break;
            case ChainStepType::Chain:
                touched = remapDuplicateId(remap.chains, step.id) || touched;
                break;
            case ChainStepType::Device:
                touched = remapDuplicateId(remap.devices, step.id) || touched;
                break;
            case ChainStepType::Segment:
                break;  // Segment steps carry no remappable ID
        }
    }

    if (touched || path.trackId == 0)
        path.trackId = remap.newTrackId;
}

void remapDuplicatedTarget(ControlTarget& target, const ChainNodePath& ownerPath,
                           const DuplicateIdRemap& remap) {
    if (target.kind == ControlTarget::Kind::ModParam && !target.devicePath.isValid()) {
        target.devicePath = ownerPath;
        return;
    }

    if (!target.devicePath.isValid())
        return;

    remapDuplicatedPath(target.devicePath, remap);
}

void remapDuplicatedLinks(MacroArray& macros, ModArray& mods, const ChainNodePath& ownerPath,
                          const DuplicateIdRemap& remap) {
    for (auto& macro : macros) {
        for (auto& link : macro.links)
            remapDuplicatedTarget(link.target, ownerPath, remap);
    }

    for (auto& mod : mods) {
        for (auto& link : mod.links)
            remapDuplicatedTarget(link.target, ownerPath, remap);
    }
}

juce::String formatClipIds(const std::vector<ClipId>& clipIds) {
    juce::String text("[");
    for (size_t i = 0; i < clipIds.size(); ++i) {
        if (i > 0)
            text << ",";
        text << clipIds[i];
    }
    text << "]";
    return text;
}

juce::String stripDuplicateRuntimePluginState(const juce::String& pluginState) {
    if (pluginState.isEmpty())
        return pluginState;

    auto xml = juce::parseXML(pluginState);
    if (!xml)
        return pluginState;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
        return pluginState;

    stripTracktionIdsRecursive(state);
    stripModifierAssignmentsRecursive(state);

    if (auto strippedXml = state.createXml())
        return strippedXml->toString();

    return pluginState;
}

void scanEmbeddedDeviceIds(const juce::ValueTree& tree, int& maxDeviceId) {
    static const juce::Identifier embeddedDeviceIdProp("magdaDeviceId");

    if (tree.hasProperty(embeddedDeviceIdProp)) {
        const int embeddedDeviceId = static_cast<int>(tree.getProperty(embeddedDeviceIdProp));
        if (embeddedDeviceId != INVALID_DEVICE_ID)
            maxDeviceId = std::max(maxDeviceId, embeddedDeviceId);
    }

    for (int i = 0; i < tree.getNumChildren(); ++i)
        scanEmbeddedDeviceIds(tree.getChild(i), maxDeviceId);
}

void scanEmbeddedDeviceIds(const juce::String& pluginState, int& maxDeviceId) {
    if (pluginState.isEmpty())
        return;

    auto xml = juce::parseXML(pluginState);
    if (!xml)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (state.isValid())
        scanEmbeddedDeviceIds(state, maxDeviceId);
}

void remapDuplicatedElements(std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
                             const DuplicateIdRemap& remap) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            auto devicePath = parentPath;
            if (parentPath.isTrackLevel) {
                devicePath = ChainNodePath::topLevelDevice(remap.newTrackId, device.id);
            } else {
                devicePath = parentPath.withDevice(device.id);
            }
            remapDuplicatedLinks(device.macros, device.mods, devicePath, remap);
            device.pluginState = stripDuplicateRuntimePluginState(device.pluginState);
        } else if (magda::isRack(element)) {
            auto& rack = magda::getRack(element);
            auto rackPath = parentPath.isTrackLevel ? ChainNodePath::rack(remap.newTrackId, rack.id)
                                                    : parentPath.withRack(rack.id);
            remapDuplicatedLinks(rack.macros, rack.mods, rackPath, remap);
            for (auto& chain : rack.chains)
                remapDuplicatedElements(chain.elements, rackPath.withChain(chain.id), remap);
        }
    }
}

ChainNodePath childDevicePath(const ChainNodePath& parentPath, DeviceId deviceId) {
    return parentPath.isTrackLevel ? ChainNodePath::topLevelDevice(parentPath.trackId, deviceId)
                                   : parentPath.withDevice(deviceId);
}

ChainNodePath childRackPath(const ChainNodePath& parentPath, RackId rackId) {
    return parentPath.isTrackLevel ? ChainNodePath::rack(parentPath.trackId, rackId)
                                   : parentPath.withRack(rackId);
}

void setChainElementsBypassed(std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
                              bool bypassed, std::vector<ChainNodePath>& affectedDevices) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            device.bypassed = bypassed;
            affectedDevices.push_back(childDevicePath(parentPath, device.id));
            continue;
        }

        auto& rack = magda::getRack(element);
        rack.bypassed = bypassed;
        const auto rackPath = childRackPath(parentPath, rack.id);
        for (auto& chain : rack.chains)
            setChainElementsBypassed(chain.elements, rackPath.withChain(chain.id), bypassed,
                                     affectedDevices);
    }
}

void enforcePostFxAnalysisDeviceOrder(std::vector<PostFxChainElement>& elements) {
    auto findAnalysis = [&elements](int order) {
        return std::find_if(elements.begin(), elements.end(), [order](const auto& element) {
            return postFxAnalysisDeviceOrder(element.device.pluginId) == order;
        });
    };

    auto osc = findAnalysis(0);
    auto spectrum = findAnalysis(1);
    if (osc != elements.end() && spectrum != elements.end() && spectrum < osc)
        std::iter_swap(osc, spectrum);
}

}  // namespace

TrackManager& TrackManager::getInstance() {
    static TrackManager instance;
    return instance;
}

TrackManager::TrackManager() {
    // Initialize master track info
    masterTrack_.id = MASTER_TRACK_ID;
    masterTrack_.type = TrackType::Master;
    masterTrack_.name = "Master";
    masterTrack_.colour = juce::Colours::grey;
}

// ============================================================================
// Plugin Drop → New Track Helper
// ============================================================================

DeviceInfo TrackManager::deviceInfoFromPluginObject(const juce::DynamicObject& pluginObj) {
    DeviceInfo device;
    device.name = pluginObj.getProperty("name").toString().toStdString();
    device.manufacturer = pluginObj.getProperty("manufacturer").toString().toStdString();
    auto uniqueId = pluginObj.getProperty("uniqueId").toString();
    device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                            : pluginObj.getProperty("name").toString() + "_" +
                                                  pluginObj.getProperty("format").toString();
    device.isInstrument = static_cast<bool>(pluginObj.getProperty("isInstrument"));
    if (pluginObj.hasProperty("deviceType"))
        device.deviceType =
            static_cast<DeviceType>(static_cast<int>(pluginObj.getProperty("deviceType")));
    else if (pluginObj.getProperty("subcategory").toString() == "MIDI")
        device.deviceType = DeviceType::MIDI;
    else
        device.deviceType = device.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    device.uniqueId = pluginObj.getProperty("uniqueId").toString();
    device.fileOrIdentifier = pluginObj.getProperty("fileOrIdentifier").toString();

    juce::String format = pluginObj.getProperty("format").toString();
    if (format == "VST3")
        device.format = PluginFormat::VST3;
    else if (format == "AU")
        device.format = PluginFormat::AU;
    else if (format == "VST")
        device.format = PluginFormat::VST;
    else if (format == "Internal")
        device.format = PluginFormat::Internal;

    return device;
}

TrackId TrackManager::createTrackWithPlugin(const juce::DynamicObject& pluginObj) {
    DeviceInfo device = deviceInfoFromPluginObject(pluginObj);

    // Determine track type
    TrackType trackType = TrackType::Audio;

    // Create the track named after the plugin
    juce::String pluginName = pluginObj.getProperty("name").toString();
    auto& tm = getInstance();
    TrackId newTrackId = tm.createTrack(pluginName, trackType);
    if (newTrackId == INVALID_TRACK_ID)
        return INVALID_TRACK_ID;

    // Add the device to the new track
    tm.addDeviceToTrack(newTrackId, device);

    // Select the new track
    tm.setSelectedTrack(newTrackId);

    DBG("Created track with plugin: " << pluginName << " (trackId=" << newTrackId << ")");
    return newTrackId;
}

// ============================================================================
// Track Operations
// ============================================================================

TrackId TrackManager::createTrack(const juce::String& name, TrackType type) {
    TrackInfo track;
    track.id = nextTrackId_++;
    track.type = type;
    track.name = name.isEmpty() ? generateTrackName() : name;
    track.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(tracks_.size())));

    // Set default routing
    track.audioOutputDevice = "master";  // Audio always routes to master
    track.audioInputDevice = "";         // Audio input disabled by default (enable via UI)
    // midiOutputDevice left empty - requires specific device selection

    // Assign aux bus index for Aux tracks; aux tracks never receive MIDI
    if (type == TrackType::Aux) {
        track.auxBusIndex = nextAuxBusIndex_++;
        track.midiInputDevice = "";  // Aux tracks don't receive MIDI
    } else {
        track.midiInputDevice = "all";  // MIDI listens to all inputs
    }

    TrackId trackId = track.id;
    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << trackId << ", type=" << getTrackTypeName(type)
                          << ")");

    // Initialize MIDI routing for this track if audioEngine is available
    // Aux tracks never receive MIDI; other tracks rely on selection-based routing
    if (audioEngine_ && type != TrackType::Aux) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            midiBridge->setTrackMidiInput(trackId, "all");
            midiBridge->startMonitoring(trackId);
        }
        // Don't auto-route MIDI at the TE level for every new track.
        // AudioBridge::updateMidiRoutingForSelection() will handle this
        // based on whether the track is selected or record-armed.
    }

    return trackId;
}

TrackId TrackManager::createGroupTrack(const juce::String& name) {
    juce::String groupName = name.isEmpty() ? "Group" : name;
    return createTrack(groupName, TrackType::Group);
}

void TrackManager::deleteTrack(TrackId trackId) {
    // The master track is permanent and must never be deleted. getTrack()
    // returns a valid pointer for MASTER_TRACK_ID, so the null check below
    // would not catch it; guard explicitly here, the single choke point all
    // delete entry points funnel through.
    if (trackId == MASTER_TRACK_ID)
        return;

    auto* track = getTrack(trackId);
    if (!track)
        return;

    // Clear selection because anything selected on this track (track, clips, devices, etc.)
    // will become invalid after deletion.
    auto& sm = magda::SelectionManager::getInstance();
    sm.clearSelection();

    auto& clipManager = magda::ClipManager::getInstance();
    auto clipIds = clipManager.getClipsOnTrack(trackId);
    DBG("TrackManager::deleteTrack clip cleanup trackId=" << trackId
                                                          << " clipIds=" << formatClipIds(clipIds));
    for (auto clipId : clipIds)
        clipManager.deleteClip(clipId);
    DBG("TrackManager::deleteTrack clip cleanup complete trackId="
        << trackId << " remainingClipIds=" << formatClipIds(clipManager.getClipsOnTrack(trackId)));

    // If this track has a parent, remove it from parent's children
    if (track->hasParent()) {
        if (auto* parent = getTrack(track->parentId)) {
            auto& children = parent->childIds;
            children.erase(std::remove(children.begin(), children.end(), trackId), children.end());
        }
    }

    // Clean up multi-out pairs for any instruments on this track
    for (const auto& element : track->chain.fxChainElements) {
        if (isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (device.multiOut.isMultiOut) {
                deactivateAllMultiOutPairs(trackId, device.id);
            }
        }
    }

    // If this is a group or instrument with children, recursively delete all children
    if (track->hasChildren()) {
        auto childrenCopy = track->childIds;
        for (auto childId : childrenCopy) {
            deleteTrack(childId);
        }
    }

    // Remove sends targeting this track from all other tracks
    for (auto& t : tracks_) {
        auto& sends = t.sends;
        sends.erase(
            std::remove_if(sends.begin(), sends.end(),
                           [trackId](const SendInfo& s) { return s.destTrackId == trackId; }),
            sends.end());
    }

    // Remove the track itself
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it != tracks_.end()) {
        DBG("Deleted track: " << it->name << " (id=" << trackId << ")");
        tracks_.erase(it);
        notifyTracksChanged();
    }
}

// =============================================================================
// Multi-Output Management
// =============================================================================

TrackId TrackManager::activateMultiOutPair(TrackId parentTrackId, DeviceId deviceId,
                                           int pairIndex) {
    auto* parentTrack = getTrack(parentTrackId);
    if (!parentTrack)
        return INVALID_TRACK_ID;

    // Find the device
    DeviceInfo* device = getDevice(parentTrackId, deviceId);
    if (!device || !device->multiOut.isMultiOut)
        return INVALID_TRACK_ID;

    // Validate pair index
    if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
        return INVALID_TRACK_ID;

    auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];

    // Already active?
    if (pair.active && pair.trackId != INVALID_TRACK_ID)
        return pair.trackId;

    // Create the output track
    TrackId newTrackId = nextTrackId_++;

    TrackInfo newTrack;
    newTrack.id = newTrackId;
    newTrack.type = TrackType::MultiOut;
    newTrack.name = device->name + ": " + pair.name;
    newTrack.colour = parentTrack->colour;
    newTrack.audioOutputDevice = parentTrack->audioOutputDevice;

    // Set the multi-out link (keeps routing reference — NOT parent-child hierarchy)
    newTrack.multiOutLink = MultiOutTrackLink{parentTrackId, deviceId, pairIndex};

    // Insert after the parent track (and any existing multi-out siblings) for adjacency
    auto parentIt =
        std::find_if(tracks_.begin(), tracks_.end(),
                     [parentTrackId](const TrackInfo& t) { return t.id == parentTrackId; });
    if (parentIt != tracks_.end()) {
        // Find last consecutive multi-out track for this device after the parent
        auto insertIt = parentIt + 1;
        while (insertIt != tracks_.end() && insertIt->type == TrackType::MultiOut &&
               insertIt->multiOutLink && insertIt->multiOutLink->sourceTrackId == parentTrackId) {
            ++insertIt;
        }
        tracks_.insert(insertIt, std::move(newTrack));
    } else {
        tracks_.push_back(std::move(newTrack));
    }

    // Re-fetch pointers after insert (vector reallocation invalidates them)
    parentTrack = getTrack(parentTrackId);
    device = getDevice(parentTrackId, deviceId);
    auto& pairRef = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];

    // Update the output pair state
    pairRef.active = true;
    pairRef.trackId = newTrackId;

    DBG("TrackManager: Activated multi-out pair " << pairIndex << " for device " << deviceId
                                                  << " → track " << newTrackId);

    notifyTracksChanged();
    return newTrackId;
}

void TrackManager::deactivateMultiOutPair(TrackId parentTrackId, DeviceId deviceId, int pairIndex) {
    auto* parentTrack = getTrack(parentTrackId);
    if (!parentTrack)
        return;

    DeviceInfo* device = getDevice(parentTrackId, deviceId);
    if (!device || !device->multiOut.isMultiOut)
        return;

    if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
        return;

    auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];
    if (!pair.active || pair.trackId == INVALID_TRACK_ID)
        return;

    TrackId trackToRemove = pair.trackId;

    // Remove the track
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackToRemove](const TrackInfo& t) { return t.id == trackToRemove; });
    if (it != tracks_.end()) {
        tracks_.erase(it);
    }

    // Update pair state
    pair.active = false;
    pair.trackId = INVALID_TRACK_ID;

    DBG("TrackManager: Deactivated multi-out pair " << pairIndex << " for device " << deviceId);

    notifyTracksChanged();
}

void TrackManager::deactivateAllMultiOutPairs(TrackId parentTrackId, DeviceId deviceId) {
    // Re-fetch device pointer each iteration since deactivateMultiOutPair
    // calls tracks_.erase() which can invalidate pointers
    for (int i = 0;; ++i) {
        DeviceInfo* device = getDevice(parentTrackId, deviceId);
        if (!device || !device->multiOut.isMultiOut)
            break;
        if (i >= static_cast<int>(device->multiOut.outputPairs.size()))
            break;
        if (device->multiOut.outputPairs[static_cast<size_t>(i)].active) {
            deactivateMultiOutPair(parentTrackId, deviceId, i);
        }
    }
}

void TrackManager::restoreTrack(const TrackInfo& trackInfo) {
    // Check if a track with this ID already exists
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [&trackInfo](const TrackInfo& t) { return t.id == trackInfo.id; });

    if (it != tracks_.end()) {
        DBG("Warning: Track with id=" << trackInfo.id << " already exists, skipping restore");
        return;
    }

    tracks_.push_back(trackInfo);

    // Ensure nextTrackId_ is beyond any restored track IDs
    if (trackInfo.id >= nextTrackId_) {
        nextTrackId_ = trackInfo.id + 1;
    }

    // If track has a parent, add it back to parent's children
    if (trackInfo.hasParent()) {
        if (auto* parent = getTrack(trackInfo.parentId)) {
            if (std::find(parent->childIds.begin(), parent->childIds.end(), trackInfo.id) ==
                parent->childIds.end()) {
                parent->childIds.push_back(trackInfo.id);
            }
        }
    }

    // Set up MidiBridge monitoring for restored track (same as createTrack)
    if (audioEngine_ && trackInfo.type != TrackType::Aux) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            midiBridge->setTrackMidiInput(trackInfo.id, "all");
            midiBridge->startMonitoring(trackInfo.id);
        }
    }

    notifyTracksChanged();
    DBG("Restored track: " << trackInfo.name << " (id=" << trackInfo.id << ")");
}

TrackId TrackManager::duplicateTrack(TrackId trackId, bool includeDevices) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it == tracks_.end()) {
        return INVALID_TRACK_ID;
    }

    TrackInfo newTrack = *it;
    newTrack.id = nextTrackId_++;
    newTrack.name = it->name + " Copy";
    newTrack.childIds.clear();  // Don't duplicate children references

    // Content-only duplication: strip every device section so the duplicate
    // starts clean. Post-FX and mixer-analysis are flat DeviceInfo lists that
    // the copy above brought along, so they must be cleared too - otherwise a
    // "no plugins / racks / chain elements" duplicate silently keeps them.
    if (!includeDevices) {
        newTrack.chain.fxChainElements.clear();
        newTrack.chain.postFxChainElements.clear();
        newTrack.chain.mixerAnalysisElements.clear();
    }

    // Reassign all device/rack/chain IDs so the duplicate gets its own
    // plugin instances in the audio engine (sharing IDs = no audio).
    DuplicateIdRemap remap;
    remap.oldTrackId = trackId;
    remap.newTrackId = newTrack.id;

    std::function<void(std::vector<ChainElement>&)> reassignIds;
    reassignIds = [&](std::vector<ChainElement>& elements) {
        for (auto& element : elements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                const auto oldDeviceId = device.id;
                device.id = nextFxDeviceId_++;
                remap.devices[oldDeviceId] = device.id;
            } else if (magda::isRack(element)) {
                auto& rack = magda::getRack(element);
                const auto oldRackId = rack.id;
                rack.id = nextRackId_++;
                remap.racks[oldRackId] = rack.id;
                for (auto& chain : rack.chains) {
                    const auto oldChainId = chain.id;
                    chain.id = nextChainId_++;
                    remap.chains[oldChainId] = chain.id;
                    reassignIds(chain.elements);
                }
            }
        }
    };
    reassignIds(newTrack.chain.fxChainElements);
    remapDuplicatedLinks(newTrack.macros, newTrack.mods, ChainNodePath::trackLevel(newTrack.id),
                         remap);
    remapDuplicatedElements(newTrack.chain.fxChainElements, ChainNodePath::trackLevel(newTrack.id),
                            remap);

    // Flat post-FX / mixer-analysis sections each carry their own section-local
    // DeviceId counter, so the copied devices must be re-stamped with fresh ids
    // from the right counter (sharing ids = sharing audio-engine plugin slots).
    // Empty when !includeDevices, so these loops are no-ops in that case.
    for (auto& element : newTrack.chain.postFxChainElements)
        element.device.id = nextPostFxDeviceId_++;
    for (auto& element : newTrack.chain.mixerAnalysisElements)
        element.device.id = nextMixerAnalysisDeviceId_++;

    // Log all device IDs after reassignment
    DBG("duplicateTrack: original trackId=" << trackId << " -> newTrackId=" << newTrack.id);
    std::function<void(const std::vector<ChainElement>&, int)> logElements;
    logElements = [&](const std::vector<ChainElement>& elements, int depth) {
        for (const auto& element : elements) {
            juce::String indent;
            for (int d = 0; d < depth; ++d)
                indent += "  ";
            if (magda::isDevice(element)) {
                const auto& dev = magda::getDevice(element);
                DBG("  " << indent << "device: " << dev.name << " id=" << dev.id
                         << " pluginState.len=" << dev.pluginState.length());
            } else if (magda::isRack(element)) {
                const auto& rack = magda::getRack(element);
                DBG("  " << indent << "rack id=" << rack.id
                         << " chains=" << (int)rack.chains.size());
                for (const auto& chain : rack.chains) {
                    DBG("  " << indent << "  chain id=" << chain.id);
                    logElements(chain.elements, depth + 2);
                }
            }
        }
    };
    logElements(newTrack.chain.fxChainElements, 0);

    // Aux tracks need a unique bus index
    if (newTrack.type == TrackType::Aux) {
        newTrack.auxBusIndex = nextAuxBusIndex_++;
    }

    // MultiOut links and output pairs reference the original track — clear them
    newTrack.multiOutLink.reset();

    std::function<void(std::vector<ChainElement>&)> clearMultiOutPairs;
    clearMultiOutPairs = [&](std::vector<ChainElement>& elements) {
        for (auto& element : elements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                for (auto& pair : device.multiOut.outputPairs) {
                    pair.active = false;
                    pair.trackId = INVALID_TRACK_ID;
                }
            } else if (magda::isRack(element)) {
                auto& rack = magda::getRack(element);
                for (auto& chain : rack.chains)
                    clearMultiOutPairs(chain.elements);
            }
        }
    };
    clearMultiOutPairs(newTrack.chain.fxChainElements);

    TrackId newId = newTrack.id;

    // Insert after the original
    auto insertPos = it + 1;
    tracks_.insert(insertPos, newTrack);

    // If the original had a parent, add the copy to the same parent
    if (newTrack.hasParent()) {
        if (auto* parent = getTrack(newTrack.parentId)) {
            parent->childIds.push_back(newId);
        }
    }

    // Set up MIDI monitoring (same as createTrack)
    if (audioEngine_ && newTrack.type != TrackType::Aux) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            midiBridge->setTrackMidiInput(newId, newTrack.midiInputDevice);
            midiBridge->startMonitoring(newId);
        }
    }

    notifyTracksChanged();
    DBG("Duplicated track: " << newTrack.name << " (id=" << newId << ")");
    return newId;
}

void TrackManager::moveTrack(TrackId trackId, int newIndex) {
    int currentIndex = getTrackIndex(trackId);
    if (currentIndex < 0 || newIndex < 0 || newIndex >= static_cast<int>(tracks_.size())) {
        return;
    }

    if (currentIndex != newIndex) {
        TrackInfo track = tracks_[currentIndex];
        tracks_.erase(tracks_.begin() + currentIndex);
        tracks_.insert(tracks_.begin() + newIndex, track);
        notifyTracksChanged();
    }
}

// ============================================================================
// Hierarchy Operations
// ============================================================================

void TrackManager::syncMultiOutChildOutputsForSource(TrackId sourceTrackId) {
    auto* sourceTrack = getTrack(sourceTrackId);
    if (!sourceTrack)
        return;

    for (auto& track : tracks_) {
        if (track.type != TrackType::MultiOut || !track.multiOutLink ||
            track.multiOutLink->sourceTrackId != sourceTrackId)
            continue;

        // If the multi-out track is explicitly grouped, its own group routing
        // wins. Otherwise it follows the source instrument track's output.
        if (track.hasParent())
            continue;

        if (track.audioOutputDevice == sourceTrack->audioOutputDevice)
            continue;

        track.audioOutputDevice = sourceTrack->audioOutputDevice;
        notifyTrackPropertyChanged(track.id);
    }
}

void TrackManager::addTrackToGroup(TrackId trackId, TrackId groupId) {
    auto* track = getTrack(trackId);
    auto* group = getTrack(groupId);

    if (!track || !group || !group->isGroup()) {
        DBG("addTrackToGroup failed: invalid track or group");
        return;
    }

    // Prevent adding a group to itself or to its descendants
    if (trackId == groupId)
        return;
    auto descendants = getAllDescendants(trackId);
    if (std::find(descendants.begin(), descendants.end(), groupId) != descendants.end()) {
        DBG("Cannot add group to its own descendant");
        return;
    }

    // Remove from current parent if any
    removeTrackFromGroup(trackId);

    // Add to new parent
    track->parentId = groupId;
    group->childIds.push_back(trackId);

    // Auto-route child's audio output to the group track.
    track->audioOutputDevice = "track:" + juce::String(groupId);
    notifyTrackPropertyChanged(trackId);
    syncMultiOutChildOutputsForSource(trackId);

    notifyTracksChanged();
    DBG("Added track " << track->name << " to group " << group->name);
}

void TrackManager::moveChildWithinGroup(TrackId childId, TrackId beforeChildId) {
    if (childId == beforeChildId)
        return;
    auto* track = getTrack(childId);
    if (!track || !track->hasParent())
        return;
    auto* parent = getTrack(track->parentId);
    if (!parent)
        return;

    auto& children = parent->childIds;
    auto cur = std::find(children.begin(), children.end(), childId);
    if (cur == children.end())
        return;
    children.erase(cur);

    // Insert before beforeChildId, or append when it's absent / INVALID.
    auto insertPos = (beforeChildId == INVALID_TRACK_ID)
                         ? children.end()
                         : std::find(children.begin(), children.end(), beforeChildId);
    children.insert(insertPos, childId);

    notifyTracksChanged();
}

void TrackManager::removeTrackFromGroup(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (!track || !track->hasParent())
        return;

    if (auto* parent = getTrack(track->parentId)) {
        auto& children = parent->childIds;
        children.erase(std::remove(children.begin(), children.end(), trackId), children.end());
    }

    track->parentId = INVALID_TRACK_ID;

    // Revert audio output to master when removed from group
    track->audioOutputDevice = "master";
    notifyTrackPropertyChanged(trackId);
    syncMultiOutChildOutputsForSource(trackId);

    notifyTracksChanged();
}

TrackId TrackManager::createTrackInGroup(TrackId groupId, const juce::String& name,
                                         TrackType type) {
    auto* group = getTrack(groupId);
    if (!group || !group->isGroup()) {
        DBG("createTrackInGroup failed: invalid group");
        return INVALID_TRACK_ID;
    }

    TrackId newId = createTrack(name, type);
    addTrackToGroup(newId, groupId);
    return newId;
}

std::vector<TrackId> TrackManager::getChildTracks(TrackId groupId) const {
    const auto* group = getTrack(groupId);
    if (!group)
        return {};
    return group->childIds;
}

std::vector<TrackId> TrackManager::getTopLevelTracks() const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isTopLevel()) {
            result.push_back(track.id);
        }
    }
    return result;
}

std::vector<TrackId> TrackManager::getAllDescendants(TrackId trackId) const {
    std::vector<TrackId> result;
    const auto* track = getTrack(trackId);
    if (!track)
        return result;

    // BFS to collect all descendants
    std::vector<TrackId> toProcess = track->childIds;
    while (!toProcess.empty()) {
        TrackId current = toProcess.back();
        toProcess.pop_back();
        result.push_back(current);

        if (const auto* child = getTrack(current)) {
            for (auto grandchildId : child->childIds) {
                toProcess.push_back(grandchildId);
            }
        }
    }
    return result;
}

// ============================================================================
// Access
// ============================================================================

TrackInfo* TrackManager::getTrack(TrackId trackId) {
    if (trackId == MASTER_TRACK_ID)
        return &masterTrack_;
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

const TrackInfo* TrackManager::getTrack(TrackId trackId) const {
    if (trackId == MASTER_TRACK_ID)
        return &masterTrack_;
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

int TrackManager::getTrackIndex(TrackId trackId) const {
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id == trackId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// Track Property Setters
// ============================================================================

void TrackManager::setTrackName(TrackId trackId, const juce::String& name) {
    if (auto* track = getTrack(trackId)) {
        track->name = name;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackColour(TrackId trackId, juce::Colour colour) {
    if (auto* track = getTrack(trackId)) {
        track->colour = colour;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackVolume(TrackId trackId, float volume, bool fromAutomation) {
    // The master is authoritative in masterChannel_; route track-level writes to
    // the master setter so every observer (inspector, mixer, headers) stays in sync.
    if (trackId == MASTER_TRACK_ID) {
        setMasterVolume(juce::jlimit(0.0f, 2.0f, volume));
        return;
    }
    if (auto* track = getTrack(trackId)) {
        // Allow up to +6dB gain (10^(6/20) ≈ 2.0)
        track->volume = juce::jlimit(0.0f, 2.0f, volume);
        if (!fromAutomation)
            track->manualVolume = track->volume;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackPan(TrackId trackId, float pan, bool fromAutomation) {
    if (trackId == MASTER_TRACK_ID) {
        setMasterPan(juce::jlimit(-1.0f, 1.0f, pan));
        return;
    }
    if (auto* track = getTrack(trackId)) {
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        if (!fromAutomation)
            track->manualPan = track->pan;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackMuted(TrackId trackId, bool muted) {
    if (trackId == MASTER_TRACK_ID) {
        setMasterMuted(muted);
        return;
    }
    if (auto* track = getTrack(trackId)) {
        track->muted = muted;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackSoloed(TrackId trackId, bool soloed) {
    if (trackId == MASTER_TRACK_ID) {
        setMasterSoloed(soloed);
        return;
    }
    if (auto* track = getTrack(trackId)) {
        track->soloed = soloed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackRecordArmed(TrackId trackId, bool armed) {
    if (auto* track = getTrack(trackId)) {
        track->recordArmed = armed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackInputMonitor(TrackId trackId, InputMonitorMode mode) {
    if (auto* track = getTrack(trackId)) {
        track->inputMonitor = mode;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackFrozen(TrackId trackId, bool frozen) {
    if (auto* track = getTrack(trackId)) {
        track->frozen = frozen;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackPlaybackMode(TrackId trackId, TrackPlaybackMode mode) {
    if (auto* track = getTrack(trackId)) {
        if (track->playbackMode == mode)
            return;
        track->playbackMode = mode;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setAllTracksPlaybackMode(TrackPlaybackMode mode) {
    for (const auto& track : tracks_) {
        setTrackPlaybackMode(track.id, mode);
    }
}

bool TrackManager::isAnyTrackInSessionMode() const {
    for (const auto& track : tracks_) {
        if (track.playbackMode == TrackPlaybackMode::Session)
            return true;
    }
    return false;
}

void TrackManager::setTrackType(TrackId trackId, TrackType type) {
    if (auto* track = getTrack(trackId)) {
        // Don't allow changing type if track has children (group tracks)
        if (track->hasChildren() && type != TrackType::Group) {
            DBG("Cannot change type of group track with children");
            return;
        }
        track->type = type;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setAudioEngine(AudioEngine* audioEngine) {
    audioEngine_ = audioEngine;

    // Sync existing tracks' MIDI routing (in case tracks were created before engine was set)
    // Only set up MidiBridge monitoring; TE-level MIDI routing is handled by
    // AudioBridge::updateMidiRoutingForSelection() based on selection/arm state.
    if (audioEngine_) {
        for (const auto& track : tracks_) {
            if (!track.midiInputDevice.isEmpty() && track.type != TrackType::Aux) {
                if (auto* midiBridge = audioEngine_->getMidiBridge()) {
                    midiBridge->setTrackMidiInput(track.id, track.midiInputDevice);
                    midiBridge->startMonitoring(track.id);
                }
                DBG("Synced MIDI monitoring for track " << track.id << ": "
                                                        << track.midiInputDevice);
            }
        }
    }
}

void TrackManager::previewNote(TrackId trackId, int noteNumber, int velocity, bool isNoteOn) {
    DBG("TrackManager::previewNote - Track=" << trackId << ", Note=" << noteNumber << ", Velocity="
                                             << velocity << ", On=" << (isNoteOn ? "YES" : "NO"));

    // Forward to engine wrapper for playback through track's instruments
    if (audioEngine_) {
        auto* track = getTrack(trackId);
        if (track) {
            DBG("TrackManager: Found track, forwarding to engine");
            // Convert TrackId to engine track ID string
            audioEngine_->previewNoteOnTrack(std::to_string(trackId), noteNumber, velocity,
                                             isNoteOn);
        } else {
            DBG("TrackManager: WARNING - Track not found!");
        }
    } else {
        DBG("TrackManager: WARNING - No audio engine!");
    }
}

// ============================================================================
// Track Routing Setters
// ============================================================================

void TrackManager::setTrackMidiInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    // Aux tracks never receive MIDI
    if (track->type == TrackType::Aux) {
        DBG("Cannot set MIDI input on aux track " << trackId);
        return;
    }

    DBG("TrackManager::setTrackMidiInput - trackId=" << trackId << " deviceId='" << deviceId
                                                     << "'");

    // Audio and MIDI input are mutually exclusive — clear audio input when enabling MIDI
    if (!deviceId.isEmpty() && !track->audioInputDevice.isEmpty()) {
        DBG("  -> Clearing audio input (mutually exclusive with MIDI)");
        setTrackAudioInput(trackId, "");
        // Re-fetch: setTrackAudioInput triggers notifyTrackPropertyChanged which may
        // cause listeners to modify the tracks_ vector, invalidating our pointer.
        track = getTrack(trackId);
        if (!track)
            return;
    }

    // Update track state
    track->midiInputDevice = deviceId;

    // Reset held-note state and flush pending MIDI triggers for this track
    // so stale triggers from the old input don't keep LFOs running
    {
        std::lock_guard<std::mutex> lock(midiTriggerMutex_);
        midiHeldNotes_.erase(trackId);
        pendingMidiNoteOns_.erase(trackId);
        pendingMidiNoteOffs_.erase(trackId);
    }

    // Forward to MidiBridge for MIDI activity monitoring (UI indicators)
    if (audioEngine_) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            if (deviceId.isEmpty()) {
                midiBridge->clearTrackMidiInput(trackId);
                midiBridge->stopMonitoring(trackId);
            } else {
                midiBridge->setTrackMidiInput(trackId, deviceId);
                midiBridge->startMonitoring(trackId);
            }
        }

        // Forward to AudioBridge for Tracktion Engine MIDI routing (actual plugin input)
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            // Convert our deviceId to AudioBridge format
            // "all" stays as "all", empty clears routing, otherwise use the device ID
            audioBridge->setTrackMidiInput(trackId, deviceId);
        }
    }

    // Notify listeners (inspector, track headers will update)
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::setTrackMidiOutput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackMidiOutput - trackId=" << trackId << " deviceId='" << deviceId
                                                      << "'");

    // Update track state
    track->midiOutputDevice = deviceId;

    // Notify listeners (AudioBridge forwards to TrackController for TE routing)
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackAudioInput - trackId=" << trackId << " deviceId='" << deviceId
                                                      << "'");

    // Audio and MIDI input are mutually exclusive — clear MIDI input when enabling audio
    if (!deviceId.isEmpty() && !track->midiInputDevice.isEmpty()) {
        DBG("  -> Clearing MIDI input (mutually exclusive with audio)");
        setTrackMidiInput(trackId, "");
        // Re-fetch: setTrackMidiInput triggers notifyTrackPropertyChanged which may
        // cause listeners to modify the tracks_ vector, invalidating our pointer.
        track = getTrack(trackId);
        if (!track)
            return;
    }

    // Update track state
    track->audioInputDevice = deviceId;

    // Forward to AudioBridge for actual routing
    if (audioEngine_) {
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            audioBridge->setTrackAudioInput(trackId, deviceId);
        }
    }

    // Notify listeners
    notifyTrackPropertyChanged(trackId);
    syncMultiOutChildOutputsForSource(trackId);
}

void TrackManager::setTrackAudioOutput(TrackId trackId, const juce::String& routing) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackAudioOutput - trackId=" << trackId << " routing='" << routing
                                                       << "'");

    // Update track state
    track->audioOutputDevice = routing;

    // Forward to AudioBridge for actual routing
    if (audioEngine_) {
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            audioBridge->setTrackAudioOutput(trackId, routing);
        }
    }

    // Notify listeners
    notifyTrackPropertyChanged(trackId);
}

// ============================================================================
// Send Management
// ============================================================================

void TrackManager::addSend(TrackId sourceTrackId, TrackId destTrackId) {
    auto* source = getTrack(sourceTrackId);
    auto* dest = getTrack(destTrackId);
    if (!source || !dest || dest->type == TrackType::Master) {
        DBG("addSend failed: invalid source or destination");
        return;
    }

    // Tracktion Engine supports a limited number of aux buses
    if (static_cast<int>(source->sends.size()) >= MAX_SENDS_PER_TRACK) {
        DBG("addSend failed: maximum number of sends (" << MAX_SENDS_PER_TRACK << ") reached");
        return;
    }

    // Auto-assign auxBusIndex for non-Aux tracks that don't have one yet
    if (dest->auxBusIndex < 0) {
        dest->auxBusIndex = nextAuxBusIndex_++;
    }

    // Check if send already exists
    for (const auto& send : source->sends) {
        if (send.busIndex == dest->auxBusIndex) {
            return;  // Already exists
        }
    }

    SendInfo send;
    send.busIndex = dest->auxBusIndex;
    send.level = 1.0f;
    send.preFader = false;
    send.destTrackId = destTrackId;
    source->sends.push_back(send);

    notifyTrackDevicesChanged(sourceTrackId);
    notifyTrackDevicesChanged(destTrackId);
    DBG("Added send from track " << sourceTrackId << " to track " << destTrackId << " (bus "
                                 << dest->auxBusIndex << ")");
}

void TrackManager::removeSend(TrackId sourceTrackId, int busIndex) {
    auto* source = getTrack(sourceTrackId);
    if (!source) {
        return;
    }

    auto& sends = source->sends;
    sends.erase(std::remove_if(sends.begin(), sends.end(),
                               [busIndex](const SendInfo& s) { return s.busIndex == busIndex; }),
                sends.end());

    notifyTrackDevicesChanged(sourceTrackId);
}

void TrackManager::setSendLevel(TrackId sourceTrackId, int busIndex, float level,
                                bool /*fromAutomation*/) {
    auto* source = getTrack(sourceTrackId);
    if (!source) {
        return;
    }

    for (auto& send : source->sends) {
        if (send.busIndex == busIndex) {
            send.level = level;
            notifyTrackPropertyChanged(sourceTrackId);
            return;
        }
    }
}

// ============================================================================
// Signal Chain Management (Unified)
// ============================================================================

const std::vector<ChainElement>& TrackManager::getChainElements(TrackId trackId) const {
    static const std::vector<ChainElement> empty;
    if (const auto* track = getTrack(trackId)) {
        return track->chain.fxChainElements;
    }
    return empty;
}

void TrackManager::moveNode(TrackId trackId, int fromIndex, int toIndex) {
    DBG("TrackManager::moveNode trackId=" << trackId << " from=" << fromIndex << " to=" << toIndex);
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chain.fxChainElements;
        int size = static_cast<int>(elements.size());
        DBG("  elements.size()=" << size);

        if (fromIndex >= 0 && fromIndex < size && toIndex >= 0 && toIndex < size &&
            fromIndex != toIndex) {
            DBG("  performing move!");
            ChainElement element = std::move(elements[fromIndex]);
            elements.erase(elements.begin() + fromIndex);
            elements.insert(elements.begin() + toIndex, std::move(element));
            notifyTrackDevicesChanged(trackId);
        } else {
            DBG("  NOT moving: invalid indices or same position");
        }
    }
}

// ============================================================================
// Device Management on Track
// ============================================================================

void TrackManager::stampDefaultKitIfMissing(DeviceInfo& dev) {
    if (!dev.isInstrument || !dev.kitRows.empty())
        return;
    const auto identifier = PluginPreferences::identifierForDevice(dev);
    if (identifier.isEmpty())
        return;
    dev.kitRows = PluginPreferences::getInstance().defaultKitRows(identifier);
}

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if ((track->type == TrackType::Aux || track->type == TrackType::Group ||
             track->type == TrackType::Master) &&
            device.isInstrument) {
            DBG("Cannot add instrument plugin to non-instrument track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = device;
        newDevice.id = nextFxDeviceId_++;
        stampDefaultKitIfMissing(newDevice);
        if (isAnalysisDevice(newDevice.pluginId))
            newDevice.deviceType = DeviceType::Analysis;
        track->chain.fxChainElements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                             << trackId);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device,
                                        int insertIndex) {
    if (auto* track = getTrack(trackId)) {
        if ((track->type == TrackType::Aux || track->type == TrackType::Group ||
             track->type == TrackType::Master) &&
            device.isInstrument) {
            DBG("Cannot add instrument plugin to non-instrument track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = device;
        newDevice.id = nextFxDeviceId_++;
        stampDefaultKitIfMissing(newDevice);
        if (isAnalysisDevice(newDevice.pluginId))
            newDevice.deviceType = DeviceType::Analysis;

        // Clamp insert index to valid range
        int maxIndex = static_cast<int>(track->chain.fxChainElements.size());
        insertIndex = std::clamp(insertIndex, 0, maxIndex);

        // Insert at specified position
        track->chain.fxChainElements.insert(track->chain.fxChainElements.begin() + insertIndex,
                                            makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                             << trackId << " at index " << insertIndex);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

// ============================================================================
// Post-fader FX (flat device list)
// ============================================================================

const std::vector<PostFxChainElement>& TrackManager::getPostFxChainElements(TrackId trackId) const {
    static const std::vector<PostFxChainElement> empty;
    if (const auto* track = getTrack(trackId)) {
        return track->chain.postFxChainElements;
    }
    return empty;
}

DeviceId TrackManager::addDeviceToPostFx(TrackId trackId, const DeviceInfo& device) {
    const auto* track = getTrack(trackId);
    int appendIndex = track ? static_cast<int>(track->chain.postFxChainElements.size()) : 0;
    return addDeviceToPostFx(trackId, device, appendIndex);
}

DeviceId TrackManager::addDeviceToPostFx(TrackId trackId, const DeviceInfo& device,
                                         int insertIndex) {
    auto* track = getTrack(trackId);
    if (!track)
        return INVALID_DEVICE_ID;

    // Post-fader FX is effects/analysis only — nothing generates sound after
    // the fader. Instruments are rejected here; racks/nesting are already
    // unrepresentable because PostFxChainElement holds a bare DeviceInfo.
    if (device.isInstrument) {
        DBG("Cannot add instrument plugin to post-fx chain");
        return INVALID_DEVICE_ID;
    }

    // Analysis devices (oscilloscope / spectrum) are unique per kind in post-fx:
    // the header toggles rely on a 1:1 mapping. Regular FX may repeat freely.
    if (isAnalysisDevice(device.pluginId) &&
        findPostFxDevice(trackId, device.pluginId) != INVALID_DEVICE_ID) {
        DBG("Post-fx already has analysis device " << device.pluginId << "; skipping duplicate");
        return INVALID_DEVICE_ID;
    }

    DeviceInfo newDevice = device;
    newDevice.id = nextPostFxDeviceId_++;
    if (isAnalysisDevice(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;

    auto& elements = track->chain.postFxChainElements;
    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(elements.size()));
    elements.insert(elements.begin() + insertIndex, PostFxChainElement{newDevice});
    enforcePostFxAnalysisDeviceOrder(elements);
    notifyTrackDevicesChanged(trackId);
    DBG("Added post-fx device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                                 << trackId << " at index " << insertIndex);
    return newDevice.id;
}

void TrackManager::movePostFxDevice(TrackId trackId, int fromIndex, int toIndex) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    auto& elements = track->chain.postFxChainElements;
    int size = static_cast<int>(elements.size());
    if (fromIndex >= 0 && fromIndex < size && toIndex >= 0 && toIndex < size &&
        fromIndex != toIndex) {
        PostFxChainElement element = std::move(elements[fromIndex]);
        elements.erase(elements.begin() + fromIndex);
        elements.insert(elements.begin() + toIndex, std::move(element));
        enforcePostFxAnalysisDeviceOrder(elements);
        notifyTrackDevicesChanged(trackId);
    }
}

DeviceId TrackManager::findPostFxDevice(TrackId trackId, const juce::String& pluginId) const {
    if (const auto* track = getTrack(trackId)) {
        for (const auto& e : track->chain.postFxChainElements) {
            if (e.device.pluginId == pluginId)
                return e.device.id;
        }
    }
    return INVALID_DEVICE_ID;
}

const std::vector<PostFxChainElement>& TrackManager::getMixerAnalysisElements(
    TrackId trackId) const {
    static const std::vector<PostFxChainElement> empty;
    if (const auto* track = getTrack(trackId))
        return track->chain.mixerAnalysisElements;
    return empty;
}

DeviceId TrackManager::addDeviceToMixerAnalysis(TrackId trackId, const DeviceInfo& device) {
    auto* track = getTrack(trackId);
    if (!track)
        return INVALID_DEVICE_ID;
    if (device.isInstrument) {
        DBG("Cannot add instrument to mixer-analysis section");
        return INVALID_DEVICE_ID;
    }
    // Mixer-analysis is rail-managed and unique per pluginId on each track.
    if (findMixerAnalysisDevice(trackId, device.pluginId) != INVALID_DEVICE_ID) {
        DBG("Mixer-analysis already has " << device.pluginId << "; skipping duplicate");
        return INVALID_DEVICE_ID;
    }

    DeviceInfo newDevice = device;
    newDevice.id = nextMixerAnalysisDeviceId_++;
    if (isAnalysisDevice(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;
    track->chain.mixerAnalysisElements.push_back(PostFxChainElement{newDevice});
    notifyTrackDevicesChanged(trackId);
    DBG("Added mixer-analysis device: " << newDevice.name << " (id=" << newDevice.id
                                        << ") to track " << trackId);
    return newDevice.id;
}

DeviceId TrackManager::findMixerAnalysisDevice(TrackId trackId,
                                               const juce::String& pluginId) const {
    if (const auto* track = getTrack(trackId)) {
        for (const auto& e : track->chain.mixerAnalysisElements) {
            if (e.device.pluginId == pluginId)
                return e.device.id;
        }
    }
    return INVALID_DEVICE_ID;
}

void TrackManager::removeDeviceFromTrack(TrackId trackId, DeviceId deviceId) {
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chain.fxChainElements;
        auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
            return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
        });
        if (it != elements.end()) {
            DBG("Removed device: " << magda::getDevice(*it).name << " (id=" << deviceId
                                   << ") from track " << trackId);
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                ChainNodePath::topLevelDevice(trackId, deviceId));
            elements.erase(it);
            notifyTrackDevicesChanged(trackId);
            return;
        }
        // Post-fader FX list (flat device list).
        auto& postElements = track->chain.postFxChainElements;
        auto pit = std::find_if(
            postElements.begin(), postElements.end(),
            [deviceId](const PostFxChainElement& e) { return e.device.id == deviceId; });
        if (pit != postElements.end()) {
            DBG("Removed post-fx device: " << pit->device.name << " (id=" << deviceId
                                           << ") from track " << trackId);
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                ChainNodePath::postFxDevice(trackId, deviceId));
            postElements.erase(pit);
            notifyTrackDevicesChanged(trackId);
            return;
        }
        // Mixer-analysis section (rail-managed mini Oscilloscope / Spectrum).
        auto& miniElements = track->chain.mixerAnalysisElements;
        auto mit = std::find_if(
            miniElements.begin(), miniElements.end(),
            [deviceId](const PostFxChainElement& e) { return e.device.id == deviceId; });
        if (mit != miniElements.end()) {
            DBG("Removed mixer-analysis device: " << mit->device.name << " (id=" << deviceId
                                                  << ") from track " << trackId);
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                ChainNodePath::mixerAnalysisDevice(trackId, deviceId));
            miniElements.erase(mit);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

void TrackManager::setDeviceBypassed(TrackId trackId, DeviceId deviceId, bool bypassed) {
    if (auto* device = getDevice(trackId, deviceId)) {
        device->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setDeviceBypassedByPath(const ChainNodePath& devicePath, bool bypassed) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->bypassed = bypassed;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setChainBypassed(TrackId trackId, bool bypassed) {
    if (auto* track = getTrack(trackId)) {
        std::vector<ChainNodePath> affectedDevices;
        setChainElementsBypassed(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                                 bypassed, affectedDevices);
        for (const auto& devicePath : affectedDevices)
            notifyDevicePropertyChanged(devicePath);
        notifyTrackDevicesChanged(trackId);
    }
}

DeviceInfo* TrackManager::getDevice(TrackId trackId, DeviceId deviceId) {
    if (auto* track = getTrack(trackId)) {
        for (auto& element : track->chain.fxChainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
        // Post-fader FX list (flat device list).
        for (auto& e : track->chain.postFxChainElements) {
            if (e.device.id == deviceId)
                return &e.device;
        }
        // Mixer-analysis section.
        for (auto& e : track->chain.mixerAnalysisElements) {
            if (e.device.id == deviceId)
                return &e.device;
        }
    }
    return nullptr;
}

// ============================================================================
// Rack Management on Track
// ============================================================================

RackId TrackManager::addRackToTrack(TrackId trackId, const juce::String& name) {
    if (auto* track = getTrack(trackId)) {
        RackInfo rack;
        rack.id = nextRackId_++;
        rack.name = name.isEmpty() ? ("Rack " + juce::String(rack.id)) : name;

        // Add a default chain to the new rack
        ChainInfo defaultChain;
        defaultChain.id = nextChainId_++;
        defaultChain.name = "Chain 1";
        rack.chains.push_back(std::move(defaultChain));

        RackId newRackId = rack.id;
        track->chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
        notifyTrackDevicesChanged(trackId);
        DBG("Added rack: " << name << " (id=" << newRackId << ") to track " << trackId);
        return newRackId;
    }
    return INVALID_RACK_ID;
}

void TrackManager::removeRackFromTrack(TrackId trackId, RackId rackId) {
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chain.fxChainElements;
        auto it = std::find_if(elements.begin(), elements.end(), [rackId](const ChainElement& e) {
            return magda::isRack(e) && magda::getRack(e).id == rackId;
        });
        if (it != elements.end()) {
            DBG("Removed rack: " << magda::getRack(*it).name << " (id=" << rackId << ") from track "
                                 << trackId);
            elements.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

RackInfo* TrackManager::getRack(TrackId trackId, RackId rackId) {
    if (auto* track = getTrack(trackId)) {
        for (auto& element : track->chain.fxChainElements) {
            if (magda::isRack(element) && magda::getRack(element).id == rackId) {
                return &magda::getRack(element);
            }
        }
    }
    return nullptr;
}

const RackInfo* TrackManager::getRack(TrackId trackId, RackId rackId) const {
    if (const auto* track = getTrack(trackId)) {
        for (const auto& element : track->chain.fxChainElements) {
            if (magda::isRack(element) && magda::getRack(element).id == rackId) {
                return &magda::getRack(element);
            }
        }
    }
    return nullptr;
}

void TrackManager::setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) {
    if (auto* rack = getRack(trackId, rackId)) {
        rack->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setRackExpanded(TrackId trackId, RackId rackId, bool expanded) {
    if (auto* rack = getRack(trackId, rackId)) {
        rack->expanded = expanded;
        notifyTrackDevicesChanged(trackId);
    }
}

// ============================================================================
// Chain Management
// ============================================================================

RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) {
    auto* track = getTrack(rackPath.trackId);
    if (!track) {
        return nullptr;
    }

    RackInfo* currentRack = nullptr;
    ChainInfo* currentChain = nullptr;

    for (const auto& step : rackPath.steps) {
        switch (step.type) {
            case ChainStepType::Rack: {
                if (currentChain == nullptr) {
                    // Top-level rack in track's chainElements
                    for (auto& element : track->chain.fxChainElements) {
                        if (magda::isRack(element)) {
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                break;
                            }
                        }
                    }
                } else {
                    // Nested rack within a chain
                    for (auto& element : currentChain->elements) {
                        if (magda::isRack(element)) {
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                currentChain = nullptr;  // Reset chain context
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ChainStepType::Chain: {
                if (currentRack != nullptr) {
                    for (auto& chain : currentRack->chains) {
                        if (chain.id == step.id) {
                            currentChain = &chain;
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Device:
                // Devices don't contain racks, skip
                break;
            case ChainStepType::Segment:
                // Segment steps don't affect rack traversal
                break;
        }
    }

    return currentRack;
}

const RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) const {
    // const version - delegates to non-const via const_cast (safe since we return const*)
    return const_cast<TrackManager*>(this)->getRackByPath(rackPath);
}

ChainId TrackManager::addChainToRack(const ChainNodePath& rackPath, const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        ChainInfo chain;
        chain.id = nextChainId_++;
        chain.name = name.isEmpty()
                         ? ("Chain " + juce::String(static_cast<int>(rack->chains.size()) + 1))
                         : name;
        rack->chains.push_back(chain);
        notifyTrackDevicesChanged(rackPath.trackId);
        return chain.id;
    }
    return INVALID_CHAIN_ID;
}

void TrackManager::removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) {
    if (auto* rack = getRack(trackId, rackId)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            DBG("Removed chain: " << it->name << " (id=" << chainId << ") from rack " << rackId);
            chains.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

void TrackManager::removeChainByPath(const ChainNodePath& chainPath) {
    // The chainPath should end with a Chain step - we need to find the parent rack
    if (chainPath.steps.empty()) {
        DBG("removeChainByPath FAILED - empty path!");
        return;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("removeChainByPath FAILED - path doesn't end with Chain step!");
        return;
    }

    // Build path to parent rack (all steps except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Find the rack and remove the chain
    if (auto* rack = getRackByPath(rackPath)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            DBG("Removed chain via path: " << it->name << " (id=" << chainId << ")");
            chains.erase(it);
            notifyTrackDevicesChanged(chainPath.trackId);
        }
    } else {
        DBG("removeChainByPath FAILED - rack not found via path!");
    }
}

ChainInfo* TrackManager::getChain(TrackId trackId, RackId rackId, ChainId chainId) {
    if (auto* rack = getRack(trackId, rackId)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

const ChainInfo* TrackManager::getChain(TrackId trackId, RackId rackId, ChainId chainId) const {
    if (const auto* rack = getRack(trackId, rackId)) {
        const auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

void TrackManager::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->outputIndex = outputIndex;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->muted = muted;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool bypassed) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->solo = solo;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volume) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->volume = juce::jlimit(-60.0f, 6.0f, volume);  // dB range
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setRackVolume(TrackId trackId, RackId rackId, float volume) {
    if (auto* rack = getRack(trackId, rackId)) {
        rack->volume = juce::jlimit(-60.0f, 6.0f, volume);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setRackVolume(const ChainNodePath& rackPath, float volume) {
    if (auto* rack = getRackByPath(rackPath)) {
        rack->volume = juce::jlimit(-60.0f, 6.0f, volume);
        notifyTrackPropertyChanged(rackPath.trackId);
    }
}

void TrackManager::setChainExpanded(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool expanded) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->expanded = expanded;
        notifyTrackDevicesChanged(trackId);
    }
}

// ============================================================================
// Path Resolution
// ============================================================================

TrackManager::ResolvedPath TrackManager::resolvePath(const ChainNodePath& path) const {
    ResolvedPath result;

    const auto* track = getTrack(path.trackId);
    if (!track) {
        return result;
    }

    // Handle top-level device (legacy)
    if (path.topLevelDeviceId != INVALID_DEVICE_ID) {
        for (const auto& element : track->chain.fxChainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == path.topLevelDeviceId) {
                result.valid = true;
                result.device = &magda::getDevice(element);
                result.displayPath = result.device->name;
                return result;
            }
        }
        return result;
    }

    // Walk through the path steps
    juce::StringArray pathNames;
    const RackInfo* currentRack = nullptr;
    const ChainInfo* currentChain = nullptr;

    for (size_t i = 0; i < path.steps.size(); ++i) {
        const auto& step = path.steps[i];

        switch (step.type) {
            case ChainStepType::Rack: {
                if (currentChain == nullptr) {
                    // Top-level rack in track's chainElements
                    for (const auto& element : track->chain.fxChainElements) {
                        if (magda::isRack(element) && magda::getRack(element).id == step.id) {
                            currentRack = &magda::getRack(element);
                            pathNames.add(currentRack->name);
                            break;
                        }
                    }
                } else {
                    // Nested rack within a chain
                    for (const auto& element : currentChain->elements) {
                        if (magda::isRack(element) && magda::getRack(element).id == step.id) {
                            currentRack = &magda::getRack(element);
                            currentChain = nullptr;  // Reset chain context
                            pathNames.add(currentRack->name);
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Chain: {
                if (currentRack != nullptr) {
                    for (const auto& chain : currentRack->chains) {
                        if (chain.id == step.id) {
                            currentChain = &chain;
                            pathNames.add(chain.name);
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Device: {
                if (currentChain != nullptr) {
                    for (const auto& element : currentChain->elements) {
                        if (magda::isDevice(element) && magda::getDevice(element).id == step.id) {
                            result.device = &magda::getDevice(element);
                            pathNames.add(result.device->name);
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Segment:
                // Segment steps are structural markers; no display name contribution
                break;
        }
    }

    // Set result based on what we found
    if (!path.steps.empty()) {
        result.displayPath = pathNames.joinIntoString(" > ");
        result.rack = currentRack;
        result.chain = currentChain;
        result.valid = !pathNames.isEmpty();
    }

    return result;
}

// ============================================================================
// View Settings
// ============================================================================

void TrackManager::setTrackVisible(TrackId trackId, ViewMode mode, bool visible) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setVisible(mode, visible);
        // Use tracksChanged since visibility affects which tracks are displayed
        notifyTracksChanged();
    }
}

void TrackManager::setTrackLocked(TrackId trackId, ViewMode mode, bool locked) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setLocked(mode, locked);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackCollapsed(TrackId trackId, bool collapsed) {
    if (auto* track = getTrack(trackId)) {
        // Apply to all view modes so collapsed state is consistent across views
        for (auto m : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
            track->viewSettings.setCollapsed(m, collapsed);
        }
        // Use tracksChanged since collapsing affects which child tracks are displayed
        notifyTracksChanged();
    }
}

void TrackManager::setTrackHeight(TrackId trackId, ViewMode mode, int height) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setHeight(mode, juce::jmax(20, height));
        notifyTrackPropertyChanged(trackId);
    }
}

// ============================================================================
// Query Tracks by View
// ============================================================================

std::vector<TrackId> TrackManager::getVisibleTracks(ViewMode mode) const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isVisibleIn(mode)) {
            result.push_back(track.id);
        }
    }
    return result;
}

std::vector<TrackId> TrackManager::getVisibleTopLevelTracks(ViewMode mode) const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isTopLevel() && track.isVisibleIn(mode)) {
            result.push_back(track.id);
        }
    }
    return result;
}

// ============================================================================
// Track Selection
// ============================================================================

void TrackManager::setSelectedTrack(TrackId trackId) {
    if (selectedTrackId_ != trackId) {
        selectedTrackId_ = trackId;
        notifyTrackSelectionChanged(trackId);
    }
}

void TrackManager::setSelectedTracks(const std::unordered_set<TrackId>& trackIds) {
    selectedTrackIds_ = trackIds;
}

void TrackManager::setSelectedChain(TrackId trackId, RackId rackId, ChainId chainId) {
    selectedChainTrackId_ = trackId;
    selectedChainRackId_ = rackId;
    selectedChainId_ = chainId;
}

void TrackManager::clearSelectedChain() {
    selectedChainTrackId_ = INVALID_TRACK_ID;
    selectedChainRackId_ = INVALID_RACK_ID;
    selectedChainId_ = INVALID_CHAIN_ID;
}

// ============================================================================
// Master Channel
// ============================================================================

// masterChannel_ is the source of truth for the master. masterTrack_ (the
// TrackInfo that lets the master appear as a track header) is kept as a mirror
// so track-API readers see the same values, and both observer paths are
// notified so every master UI updates regardless of which it listens to.
void TrackManager::setMasterVolume(float volume) {
    masterChannel_.volume = volume;
    masterTrack_.volume = volume;
    notifyMasterChannelChanged();
    notifyTrackPropertyChanged(MASTER_TRACK_ID);
}

void TrackManager::setMasterPan(float pan) {
    masterChannel_.pan = pan;
    masterTrack_.pan = pan;
    notifyMasterChannelChanged();
    notifyTrackPropertyChanged(MASTER_TRACK_ID);
}

void TrackManager::setMasterMuted(bool muted) {
    masterChannel_.muted = muted;
    masterTrack_.muted = muted;
    notifyMasterChannelChanged();
    notifyTrackPropertyChanged(MASTER_TRACK_ID);
}

void TrackManager::setMasterSoloed(bool soloed) {
    masterChannel_.soloed = soloed;
    masterTrack_.soloed = soloed;
    notifyMasterChannelChanged();
    notifyTrackPropertyChanged(MASTER_TRACK_ID);
}

void TrackManager::setMasterVisible(ViewMode mode, bool visible) {
    masterChannel_.viewSettings.setVisible(mode, visible);
    notifyMasterChannelChanged();
}

// ============================================================================
// Listener Management
// ============================================================================

void TrackManager::addListener(TrackManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void TrackManager::removeListener(TrackManagerListener* listener) {
    if (notifyDepth_ > 0) {
        // During iteration — nullify instead of erasing to keep iterators valid
        std::replace(listeners_.begin(), listeners_.end(), listener,
                     static_cast<TrackManagerListener*>(nullptr));
    } else {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener),
                         listeners_.end());
    }
}

// ============================================================================
// Initialization
// ============================================================================

void TrackManager::createDefaultTracks(int count) {
    clearAllTracks();
    for (int i = 0; i < count; ++i) {
        createTrack();
    }
}

void TrackManager::clearAllTracks() {
    tracks_.clear();
    masterTrack_.chain.fxChainElements.clear();
    masterTrack_.chain.postFxChainElements.clear();
    masterTrack_.chain.mixerAnalysisElements.clear();
    nextTrackId_ = 1;
    nextFxDeviceId_ = 1;
    nextPostFxDeviceId_ = 1;
    nextMixerAnalysisDeviceId_ = 1;
    nextRackId_ = 1;
    nextChainId_ = 1;
    nextAuxBusIndex_ = 0;

    // Reset MIDI trigger state so stale held-note counts don't block
    // first-note-on detection after project close/reopen.
    midiHeldNotes_.clear();
    {
        std::lock_guard<std::mutex> lock(midiTriggerMutex_);
        pendingMidiNoteOns_.clear();
        pendingMidiNoteOffs_.clear();
    }

    // Sync lastBus counters to current SidechainTriggerBus values so the
    // first tick after reopen sees a delta of 0 (no phantom note burst).
    auto& bus = SidechainTriggerBus::getInstance();
    for (int i = 0; i < kMaxBusTracks; ++i) {
        lastBusNoteOn_[i] = bus.getNoteOnCounter(i);
        lastBusNoteOff_[i] = bus.getNoteOffCounter(i);
    }

    notifyTracksChanged();
}

void TrackManager::refreshIdCountersFromTracks() {
    int maxTrackId = 0;
    int maxFxDeviceId = 0;
    int maxPostFxDeviceId = 0;
    int maxMixerAnalysisDeviceId = 0;
    int maxRackId = 0;
    int maxChainId = 0;

    // Helper lambda to scan a chain element (device or rack)
    auto scanChainElement = [&](const ChainElement& element, auto& self) -> void {
        if (std::holds_alternative<DeviceInfo>(element)) {
            const auto& device = std::get<DeviceInfo>(element);
            maxFxDeviceId = std::max(maxFxDeviceId, device.id);
            scanEmbeddedDeviceIds(device.pluginState, maxFxDeviceId);
        } else if (std::holds_alternative<std::unique_ptr<RackInfo>>(element)) {
            const auto& rackPtr = std::get<std::unique_ptr<RackInfo>>(element);
            if (rackPtr) {
                maxRackId = std::max(maxRackId, rackPtr->id);

                // Scan all chains in the rack
                for (const auto& chain : rackPtr->chains) {
                    maxChainId = std::max(maxChainId, chain.id);

                    // Recursively scan elements in this chain
                    for (const auto& chainElement : chain.elements) {
                        self(chainElement, self);
                    }
                }
            }
        }
    };

    int maxAuxBusIndex = -1;

    // Scan all tracks
    for (const auto& track : tracks_) {
        maxTrackId = std::max(maxTrackId, track.id);

        if (track.auxBusIndex >= 0) {
            maxAuxBusIndex = std::max(maxAuxBusIndex, track.auxBusIndex);
        }

        // Scan the track's chain elements
        for (const auto& element : track.chain.fxChainElements) {
            scanChainElement(element, scanChainElement);
        }
        // Flat sections each have their own section-local DeviceId counter.
        for (const auto& elem : track.chain.postFxChainElements) {
            maxPostFxDeviceId = std::max(maxPostFxDeviceId, elem.device.id);
            scanEmbeddedDeviceIds(elem.device.pluginState, maxPostFxDeviceId);
        }
        for (const auto& elem : track.chain.mixerAnalysisElements) {
            maxMixerAnalysisDeviceId = std::max(maxMixerAnalysisDeviceId, elem.device.id);
            scanEmbeddedDeviceIds(elem.device.pluginState, maxMixerAnalysisDeviceId);
        }
    }

    for (const auto& element : masterTrack_.chain.fxChainElements) {
        scanChainElement(element, scanChainElement);
    }
    for (const auto& elem : masterTrack_.chain.postFxChainElements) {
        maxPostFxDeviceId = std::max(maxPostFxDeviceId, elem.device.id);
        scanEmbeddedDeviceIds(elem.device.pluginState, maxPostFxDeviceId);
    }
    for (const auto& elem : masterTrack_.chain.mixerAnalysisElements) {
        maxMixerAnalysisDeviceId = std::max(maxMixerAnalysisDeviceId, elem.device.id);
        scanEmbeddedDeviceIds(elem.device.pluginState, maxMixerAnalysisDeviceId);
    }

    // Update counters to max + 1
    nextTrackId_ = maxTrackId + 1;
    nextFxDeviceId_ = maxFxDeviceId + 1;
    nextPostFxDeviceId_ = maxPostFxDeviceId + 1;
    nextMixerAnalysisDeviceId_ = maxMixerAnalysisDeviceId + 1;
    nextRackId_ = maxRackId + 1;
    nextChainId_ = maxChainId + 1;
    nextAuxBusIndex_ = maxAuxBusIndex + 1;
}

// ============================================================================
// Private Helpers
// ============================================================================

void TrackManager::notifyTracksChanged() {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->tracksChanged();
    }
}

void TrackManager::notifyTrackPropertyChanged(int trackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->trackPropertyChanged(trackId);
    }
}

void TrackManager::notifyMasterChannelChanged() {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->masterChannelChanged();
    }
}

void TrackManager::notifyTrackSelectionChanged(TrackId trackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->trackSelectionChanged(trackId);
    }
}

void TrackManager::notifyTrackDevicesChanged(TrackId trackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->trackDevicesChanged(trackId);
    }
}

void TrackManager::notifyDeviceModifiersChanged(TrackId trackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->deviceModifiersChanged(trackId);
    }
}

void TrackManager::notifyModulationNamesChanged(TrackId trackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->modulationNamesChanged(trackId);
    }
}

void TrackManager::notifyAudioSidechainTriggered(TrackId sourceTrackId) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->audioSidechainTriggered(sourceTrackId);
    }
}

void TrackManager::notifyDevicePropertyChanged(const ChainNodePath& devicePath) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->devicePropertyChanged(devicePath);
    }
}

void TrackManager::notifyDeviceParameterChanged(const ChainNodePath& devicePath, int paramIndex,
                                                float newValue) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->deviceParameterChanged(devicePath, paramIndex, newValue);
    }
}

void TrackManager::notifyMacroValueChanged(TrackId trackId, ChainScope scope, int ownerId,
                                           int macroIndex, float value) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->macroValueChanged(trackId, scope, ownerId, macroIndex, value);
    }
}

void TrackManager::notifyModParameterChanged(TrackId trackId, const ChainNodePath& devicePath,
                                             ModId modId, int paramIndex, float value) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->modParameterChanged(trackId, devicePath, modId, paramIndex, value);
    }
}

void TrackManager::updateRackMods(const RackInfo& rack, double deltaTime) {
    // TODO: Recursively update mods in rack, chains, and nested racks
    (void)rack;
    (void)deltaTime;
}

void TrackManager::notifyModulationChanged() {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->tracksChanged();
    }
}

juce::String TrackManager::generateTrackName() const {
    return juce::String(tracks_.size() + 1) + " Track";
}

}  // namespace magda
