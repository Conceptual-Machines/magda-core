#include "TrackManager.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "../audio/AudioBridge.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/TracktionHelpers.hpp"
#include "../audio/plugins/SidechainTriggerBus.hpp"
#include "../engine/AudioEngine.hpp"
#include "ClipManager.hpp"
#include "Config.hpp"
#include "DeviceState.hpp"
#include "DrumGridPads.hpp"
#include "ModulatorEngine.hpp"
#include "PluginCapabilities.hpp"
#include "PluginPreferences.hpp"
#include "RackInfo.hpp"
#include "SelectionManager.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"

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
            case ChainStepType::PadRack:
                // A PadRack step carries the owning grid's DeviceId, so it moves
                // with the devices map like any other device id. Saying so in
                // the type is what removed the shape-matching pad remapper this
                // used to need (#2219).
                touched = remapDuplicateId(remap.devices, step.id) || touched;
                break;
            case ChainStepType::PadChain:
                break;  // Pad chain ids are rack-local and survive duplication
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

// v2 device state is captured already stripped of engine ids and modifier
// assignments (see TracktionDeviceStateBridge.hpp), so only legacy engine XML
// needs cleaning here.
juce::String stripDuplicateRuntimePluginState(const juce::String& pluginState) {
    if (pluginState.isEmpty() || !device_state::looksLikeLegacyEngineState(pluginState))
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
    static const juce::Identifier embeddedDeviceIdProp("magdaDeviceId");

    if (pluginState.isEmpty())
        return;

    // v2 device state: walk the MAGDA document (DrumGrid embeds a device id per
    // pad chain, which the id allocator has to see).
    if (auto doc = device_state::decode(pluginState)) {
        device_state::forEachNode(doc->root, [&](const device_state::Node& node) {
            if (const auto* value = node.props.getVarPointer(embeddedDeviceIdProp)) {
                const int embeddedDeviceId = static_cast<int>(*value);
                if (embeddedDeviceId != INVALID_DEVICE_ID)
                    maxDeviceId = std::max(maxDeviceId, embeddedDeviceId);
            }
        });
        return;
    }

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

            // A device's pads are chains it owns, so they are walked like a
            // rack's: their devices carry state to strip, and the grid's links
            // into them were remapped by the call above (#2207).
            if (device.pads) {
                const auto padsPath = devicePath.parentChain().withRack(device.id);
                for (auto& pad : device.pads->chains)
                    remapDuplicatedElements(pad.elements, padsPath.withChain(pad.id), remap);
            }
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

// Collect the device paths of every device in the given chain tree (racks
// recursed), without mutating anything. Path construction mirrors
// setChainElementsBypassed below.
void collectChainDevicePaths(const std::vector<ChainElement>& elements,
                             const ChainNodePath& parentPath,
                             std::vector<ChainNodePath>& outDevices) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            outDevices.push_back(childDevicePath(parentPath, magda::getDevice(element).id));
            continue;
        }
        const auto& rack = magda::getRack(element);
        const auto rackPath = childRackPath(parentPath, rack.id);
        for (const auto& chain : rack.chains)
            collectChainDevicePaths(chain.elements, rackPath.withChain(chain.id), outDevices);
    }
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
    // Keep the analysis devices (oscilloscope, spectrum, levels) in a stable,
    // canonical order among themselves without disturbing any non-analysis
    // post-FX devices' relative positions.
    std::stable_sort(elements.begin(), elements.end(), [](const auto& a, const auto& b) {
        const int oa = daw::audio::internalPostFxAnalysisOrder(a.device.pluginId);
        const int ob = daw::audio::internalPostFxAnalysisOrder(b.device.pluginId);
        if (oa < 0 || ob < 0)
            return false;  // leave non-analysis devices where they are
        return oa < ob;
    });
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
    const auto rawCategory = pluginObj.hasProperty("rawCategory")
                                 ? pluginObj.getProperty("rawCategory").toString()
                                 : pluginObj.getProperty("category").toString();
    const auto rawSubcategory = pluginObj.hasProperty("rawSubcategory")
                                    ? pluginObj.getProperty("rawSubcategory").toString()
                                    : pluginObj.getProperty("subcategory").toString();
    device.isInstrument = rawCategory.isNotEmpty()
                              ? rawCategory == "Instrument"
                              : static_cast<bool>(pluginObj.getProperty("isInstrument"));
    if (pluginObj.hasProperty("deviceType"))
        device.deviceType =
            static_cast<DeviceType>(static_cast<int>(pluginObj.getProperty("deviceType")));
    else if (rawSubcategory == "MIDI")
        device.deviceType = DeviceType::MIDI;
    else
        device.deviceType = device.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    device.browserCategoryOverride = pluginObj.getProperty("categoryOverride").toString();
    device.uniqueId = pluginObj.getProperty("uniqueId").toString();
    device.fileOrIdentifier = pluginObj.getProperty("fileOrIdentifier").toString();

    juce::String format = pluginObj.getProperty("format").toString();
    if (format == "VST3")
        device.format = PluginFormat::VST3;
    else if (format == "AU")
        device.format = PluginFormat::AU;
    else if (format == "LV2")
        device.format = PluginFormat::LV2;
    else if (format == "Internal")
        device.format = PluginFormat::Internal;

    return device;
}

TrackId TrackManager::createTrackWithPlugin(const juce::DynamicObject& pluginObj) {
    DeviceInfo device = deviceInfoFromPluginObject(pluginObj);

    // Determine track type
    TrackType trackType = TrackType::Media;

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
    // Chord track defaults to "Chord Track" (still renameable); other tracks get
    // the generic "N Track".
    track.name = !name.isEmpty()            ? name
                 : type == TrackType::Chord ? juce::String("Chord Track")
                                            : generateTrackName();
    track.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(tracks_.size())));

    // The chord-track audition (speaker) toggle is the track's mute state. Seed
    // it from the preference so chord preview can be on by default if desired.
    if (type == TrackType::Chord)
        track.muted = !Config::getInstance().getChordPreviewOnByDefault();

    // Which side of the fader the post-FX stage starts on. Per track from then
    // on (the fader tag on the post-FX panel); the master track is pinned
    // pre-fader by the compilers whatever this says.
    track.chain.postFxPostFader = Config::getInstance().getPostFxPostFaderByDefault();

    // Set default routing
    track.audioOutputDevice = "master";  // Audio always routes to master
    track.audioInputDevice = "";         // Audio input disabled by default (enable via UI)
    // midiOutputDevice left empty - requires specific device selection

    // Aux buses need a bus index.
    if (type == TrackType::Aux)
        track.auxBusIndex = nextAuxBusIndex_++;

    // Seed the default "listen to all inputs" and let the single type-invariant
    // boundary clear it (plus monitor/record-arm) for input-less tracks.
    track.midiInputDevice = "all";
    track.normalizeForType();

    TrackId trackId = track.id;
    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << trackId << ", type=" << getTrackTypeName(type)
                          << ")");

    // Register for MIDI input monitoring (no-op for input-less tracks). TE-level
    // routing is left to AudioBridge::updateMidiInputRouting() based on
    // selection / record-arm.
    startMidiMonitoring(track, "all");

    // The chord track ships with a Chord Engine (the authoring/suggestion UI
    // the chord panel binds to) followed by a default instrument, so chord
    // previews are audible out of the box. Both are internal devices; the
    // pluginIds mirror their xmlTypeNames. Done here so the menu action and
    // ensureChordTrack() both yield a fully-formed chord track.
    if (type == TrackType::Chord) {
        DeviceInfo engine;
        engine.name = "Chord Engine";
        engine.manufacturer = "MAGDA";
        engine.pluginId = "midichordengine";
        engine.uniqueId = "midichordengine";
        engine.fileOrIdentifier = "midichordengine";
        engine.isInstrument = false;
        engine.deviceType = DeviceType::MIDI;
        engine.format = PluginFormat::Internal;
        addDeviceToTrack(trackId, engine);

        DeviceInfo instrument;
        instrument.name = "4OSC";
        instrument.manufacturer = "MAGDA";
        instrument.pluginId = "4osc";
        instrument.uniqueId = "4osc";
        instrument.fileOrIdentifier = "4osc";
        instrument.isInstrument = true;
        instrument.deviceType = DeviceType::Instrument;
        instrument.format = PluginFormat::Internal;
        addDeviceToTrack(trackId, instrument);
    }

    return trackId;
}

TrackId TrackManager::createGroupTrack(const juce::String& name) {
    juce::String groupName = name.isEmpty() ? "Group" : name;
    return createTrack(groupName, TrackType::Group);
}

TrackId TrackManager::getChordTrackId() const {
    for (const auto& track : tracks_) {
        if (track.type == TrackType::Chord)
            return track.id;
    }
    return INVALID_TRACK_ID;
}

TrackId TrackManager::ensureChordTrack() {
    if (auto existing = getChordTrackId(); existing != INVALID_TRACK_ID)
        return existing;
    return createTrack("", TrackType::Chord);
}

TrackId TrackManager::groupTracks(const std::vector<TrackId>& trackIds, const juce::String& name) {
    if (trackIds.size() < 2)
        return INVALID_TRACK_ID;

    // createGroupTrack/moveTrack/addTrackToGroup each fire notifyTracksChanged();
    // coalesce them so the UI rebuilds once instead of once per grouped track.
    BatchScope batch;

    std::unordered_set<TrackId> requested(trackIds.begin(), trackIds.end());
    requested.erase(INVALID_TRACK_ID);
    requested.erase(MASTER_TRACK_ID);

    std::vector<TrackId> tracksToGroup;
    tracksToGroup.reserve(requested.size());

    for (const auto& track : tracks_) {
        if (requested.count(track.id) == 0)
            continue;

        bool hasSelectedAncestor = false;
        TrackId parentId = track.parentId;
        while (parentId != INVALID_TRACK_ID) {
            if (requested.count(parentId) != 0) {
                hasSelectedAncestor = true;
                break;
            }

            const auto* parent = getTrack(parentId);
            parentId = parent != nullptr ? parent->parentId : INVALID_TRACK_ID;
        }

        if (!hasSelectedAncestor)
            tracksToGroup.push_back(track.id);
    }

    if (tracksToGroup.size() < 2)
        return INVALID_TRACK_ID;

    int firstSelectedIndex = getTrackIndex(tracksToGroup.front());
    if (firstSelectedIndex < 0)
        return INVALID_TRACK_ID;

    TrackId parentGroupId = INVALID_TRACK_ID;
    bool hasSharedParent = true;
    if (const auto* firstTrack = getTrack(tracksToGroup.front())) {
        parentGroupId = firstTrack->parentId;
        for (auto trackId : tracksToGroup) {
            const auto* track = getTrack(trackId);
            if (!track || track->parentId != parentGroupId) {
                hasSharedParent = false;
                break;
            }
        }
    } else {
        hasSharedParent = false;
    }

    int parentInsertIndex = -1;
    if (hasSharedParent && parentGroupId != INVALID_TRACK_ID) {
        if (const auto* parent = getTrack(parentGroupId)) {
            for (auto trackId : tracksToGroup) {
                auto it = std::find(parent->childIds.begin(), parent->childIds.end(), trackId);
                if (it == parent->childIds.end())
                    continue;

                const int childIndex =
                    static_cast<int>(std::distance(parent->childIds.begin(), it));
                parentInsertIndex =
                    parentInsertIndex < 0 ? childIndex : std::min(parentInsertIndex, childIndex);
            }
        }
    }

    TrackId groupId = createGroupTrack(name);
    if (groupId == INVALID_TRACK_ID)
        return INVALID_TRACK_ID;

    moveTrack(groupId, firstSelectedIndex);

    if (hasSharedParent && parentGroupId != INVALID_TRACK_ID) {
        addTrackToGroup(groupId, parentGroupId);

        if (auto* parent = getTrack(parentGroupId)) {
            auto& siblings = parent->childIds;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), groupId), siblings.end());

            auto insertIt = siblings.end();
            if (parentInsertIndex >= 0) {
                insertIt = siblings.begin() +
                           juce::jlimit(0, static_cast<int>(siblings.size()), parentInsertIndex);
            }
            siblings.insert(insertIt, groupId);
        }
    }

    for (auto trackId : tracksToGroup)
        addTrackToGroup(trackId, groupId);

    return groupId;
}

std::vector<TrackId> TrackManager::ungroupTrack(TrackId groupId) {
    auto* group = getTrack(groupId);
    if (!group || !group->isGroup() || group->childIds.empty())
        return {};

    const auto children = group->childIds;
    const TrackId parentGroupId = group->parentId;
    const int groupIndex = getTrackIndex(groupId);
    if (groupIndex < 0)
        return {};

    int groupSiblingIndex = -1;
    if (auto* parent = getTrack(parentGroupId)) {
        auto it = std::find(parent->childIds.begin(), parent->childIds.end(), groupId);
        if (it != parent->childIds.end())
            groupSiblingIndex = static_cast<int>(std::distance(parent->childIds.begin(), it));
    }

    for (auto childId : children) {
        auto* child = getTrack(childId);
        if (!child)
            continue;

        child->parentId = parentGroupId;
        child->audioOutputDevice =
            parentGroupId != INVALID_TRACK_ID ? "track:" + juce::String(parentGroupId) : "master";
        notifyTrackPropertyChanged(childId);
        syncMultiOutChildOutputsForSource(childId);
    }

    group = getTrack(groupId);
    if (!group)
        return {};
    group->childIds.clear();

    if (auto* parent = getTrack(parentGroupId)) {
        auto& siblings = parent->childIds;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), groupId), siblings.end());

        auto insertIt = siblings.end();
        if (groupSiblingIndex >= 0) {
            insertIt = siblings.begin() +
                       juce::jlimit(0, static_cast<int>(siblings.size()), groupSiblingIndex);
        }
        siblings.insert(insertIt, children.begin(), children.end());
    }

    int insertAt = groupIndex + 1;
    for (auto childId : children) {
        int currentIndex = getTrackIndex(childId);
        if (currentIndex < 0)
            continue;

        int adjustedTarget = insertAt;
        if (currentIndex < adjustedTarget)
            adjustedTarget--;

        moveTrack(childId, adjustedTarget);
        insertAt = getTrackIndex(childId) + 1;
    }

    deleteTrack(groupId);
    notifyTracksChanged();
    return children;
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

    // Clear internal track-input routing on tracks listening to this track.
    // Collect ids first: the setters notify listeners, which may mutate tracks_.
    const juce::String trackInputId = "track:" + juce::String(trackId);
    std::vector<TrackId> audioInputListeners;
    std::vector<TrackId> midiInputListeners;
    for (const auto& t : tracks_) {
        if (t.audioInputDevice == trackInputId)
            audioInputListeners.push_back(t.id);
        if (t.midiInputDevice == trackInputId)
            midiInputListeners.push_back(t.id);
    }
    for (auto listenerId : audioInputListeners)
        setTrackAudioInput(listenerId, "");
    for (auto listenerId : midiInputListeners)
        setTrackMidiInput(listenerId, "");

    // Clear sidechain configs (devices and racks) referencing this track on
    // every other track including master, like the send / input-routing
    // sweeps above - otherwise the UI keeps showing a source that no longer
    // exists and the stale id gets serialized.
    {
        std::vector<DeviceId> clearedDevices;
        auto clearInElements = [&](auto&& self, std::vector<ChainElement>& elements) -> bool {
            bool cleared = false;
            for (auto& element : elements) {
                if (magda::isDevice(element)) {
                    auto& device = magda::getDevice(element);
                    if (device.sidechain.sourceTrackId == trackId) {
                        device.sidechain = {};
                        clearedDevices.push_back(device.id);
                        cleared = true;
                    }
                } else if (magda::isRack(element)) {
                    auto& rack = magda::getRack(element);
                    if (rack.sidechain.sourceTrackId == trackId) {
                        rack.sidechain = {};
                        cleared = true;
                    }
                    for (auto& chain : rack.chains)
                        cleared = self(self, chain.elements) || cleared;
                }
            }
            return cleared;
        };
        std::vector<TrackId> sidechainAffectedTracks;
        for (auto& t : tracks_) {
            if (t.id == trackId)
                continue;
            if (clearInElements(clearInElements, t.chain.fxChainElements))
                sidechainAffectedTracks.push_back(t.id);
        }
        if (clearInElements(clearInElements, masterTrack_.chain.fxChainElements))
            sidechainAffectedTracks.push_back(MASTER_TRACK_ID);
        for (auto deviceId : clearedDevices)
            notifyDevicePropertyChanged(findDevicePath(deviceId));
        // Re-sync the affected tracks' modifiers/sidechain caches (mirrors
        // setSidechainSource / setRackSidechainSource).
        for (auto affectedId : sidechainAffectedTracks)
            notifyDeviceModifiersChanged(affectedId);
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

namespace {

/// The multi-out device @p deviceId names, wherever it sits on @p parentTrackId.
///
/// `getDevice(trackId, deviceId)` walks the track's flat lists only, so a
/// multi-out instrument inside a rack chain answered nothing and its pairs
/// could never be activated. A Drum Grid reaches this on any pad given a bus,
/// and nesting one in a rack is explicitly supported (#2211).
DeviceInfo* multiOutDevice(TrackManager& tm, TrackId parentTrackId, DeviceId deviceId) {
    if (auto* device = tm.getDevice(parentTrackId, deviceId))
        return device;

    const auto path = tm.findDevicePath(deviceId);
    return path.isValid() && path.trackId == parentTrackId ? tm.getDeviceInChainByPath(path)
                                                           : nullptr;
}

}  // namespace

TrackId TrackManager::multiOutChildTrack(TrackId parentTrackId, DeviceId deviceId,
                                         int pairIndex) const {
    // The child tracks are asked, because they own the assignment. A device's
    // description of its pairs says nothing about where they go, so there is no
    // second answer that can disagree with this one (#2220).
    if (parentTrackId == INVALID_TRACK_ID || deviceId == INVALID_DEVICE_ID || pairIndex < 0)
        return INVALID_TRACK_ID;

    for (const auto& track : tracks_) {
        if (!track.multiOutLink)
            continue;
        const auto& link = *track.multiOutLink;
        if (link.sourceTrackId == parentTrackId && link.sourceDeviceId == deviceId &&
            link.outputPairIndex == pairIndex)
            return track.id;
    }
    return INVALID_TRACK_ID;
}

TrackId TrackManager::activateMultiOutPair(TrackId parentTrackId, DeviceId deviceId,
                                           int pairIndex) {
    auto* parentTrack = getTrack(parentTrackId);
    if (!parentTrack)
        return INVALID_TRACK_ID;

    // Find the device
    DeviceInfo* device = multiOutDevice(*this, parentTrackId, deviceId);
    if (!device || !device->multiOut.isMultiOut)
        return INVALID_TRACK_ID;

    // Validate pair index
    if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
        return INVALID_TRACK_ID;

    const auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];

    // Already driving a child track? Asked of the child tracks, which own the
    // assignment, rather than of a flag on the device (#2220).
    if (const auto existing = multiOutChildTrack(parentTrackId, deviceId, pairIndex);
        existing != INVALID_TRACK_ID)
        return existing;

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

    // Nothing to write back on the device: inserting the child track with its
    // link IS the assignment, and every reader derives from it (#2220).

    DBG("TrackManager: Activated multi-out pair " << pairIndex << " for device " << deviceId
                                                  << " -> track " << newTrackId);

    notifyTracksChanged();
    return newTrackId;
}

void TrackManager::deactivateMultiOutPair(TrackId parentTrackId, DeviceId deviceId, int pairIndex) {
    auto* parentTrack = getTrack(parentTrackId);
    if (!parentTrack)
        return;

    DeviceInfo* device = multiOutDevice(*this, parentTrackId, deviceId);
    if (!device || !device->multiOut.isMultiOut)
        return;

    if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
        return;

    const auto trackToRemove = multiOutChildTrack(parentTrackId, deviceId, pairIndex);
    if (trackToRemove == INVALID_TRACK_ID)
        return;

    // Removing the child track removes the assignment: the link went with it,
    // and there is no second copy to clear (#2220).
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackToRemove](const TrackInfo& t) { return t.id == trackToRemove; });
    if (it != tracks_.end()) {
        tracks_.erase(it);
    }

    DBG("TrackManager: Deactivated multi-out pair " << pairIndex << " for device " << deviceId);

    notifyTracksChanged();
}

void TrackManager::deactivateAllMultiOutPairs(TrackId parentTrackId, DeviceId deviceId) {
    // Re-fetch device pointer each iteration since deactivateMultiOutPair
    // calls tracks_.erase() which can invalidate pointers
    for (int i = 0;; ++i) {
        DeviceInfo* device = multiOutDevice(*this, parentTrackId, deviceId);
        if (!device || !device->multiOut.isMultiOut)
            break;
        if (i >= static_cast<int>(device->multiOut.outputPairs.size()))
            break;
        if (multiOutPairIsActive(parentTrackId, deviceId, i)) {
            deactivateMultiOutPair(parentTrackId, deviceId, i);
        }
    }
}

void TrackManager::startMidiMonitoring(const TrackInfo& track, const juce::String& deviceId) {
    // Input-less tracks (Aux, Group) never receive MIDI. TE-level routing is
    // owned by AudioBridge::updateMidiInputRouting(); this only wires the
    // MidiBridge activity monitor.
    if (!audioEngine_ || !track.takesExternalInput())
        return;
    if (auto* midiBridge = audioEngine_->getMidiBridge()) {
        midiBridge->setTrackMidiInput(track.id, deviceId);
        midiBridge->startMonitoring(track.id);
    }
}

TrackRestorePosition TrackManager::restorePositionOf(TrackId trackId) const {
    TrackRestorePosition position;
    position.trackIndex = getTrackIndex(trackId);

    if (const auto* track = getTrack(trackId); track != nullptr && track->hasParent()) {
        if (const auto* parent = getTrack(track->parentId)) {
            const auto found = std::find(parent->childIds.begin(), parent->childIds.end(), trackId);
            if (found != parent->childIds.end())
                position.siblingIndex =
                    static_cast<int>(std::distance(parent->childIds.begin(), found));
        }
    }

    return position;
}

namespace {

/// Every device and rack under @p elements that is sidechained to one of
/// @p doomed, with the path that addresses it.
void collectSidechainsInto(const std::vector<ChainElement>& elements,
                           const ChainNodePath& parentChain, const std::set<TrackId>& doomed,
                           std::vector<ExternalTrackRouting::Sidechain>& found) {
    const bool topLevel = parentChain.steps.empty();
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (doomed.count(device.sidechain.sourceTrackId) == 1)
                found.push_back({topLevel
                                     ? ChainNodePath::topLevelDevice(parentChain.trackId, device.id)
                                     : parentChain.withDevice(device.id),
                                 device.sidechain});
            continue;
        }

        const auto& rack = magda::getRack(element);
        const auto rackPath = topLevel ? ChainNodePath::rack(parentChain.trackId, rack.id)
                                       : parentChain.withRack(rack.id);
        if (doomed.count(rack.sidechain.sourceTrackId) == 1)
            found.push_back({rackPath, rack.sidechain});
        for (const auto& chain : rack.chains)
            collectSidechainsInto(chain.elements, rackPath.withChain(chain.id), doomed, found);
    }
}

}  // namespace

ExternalTrackRouting TrackManager::externalRoutingInto(const std::vector<TrackId>& trackIds) const {
    ExternalTrackRouting routing;
    const std::set<TrackId> doomed(trackIds.begin(), trackIds.end());

    const auto listensToADoomedTrack = [&doomed](const juce::String& input) {
        for (auto id : doomed)
            if (input == "track:" + juce::String(id))
                return true;
        return false;
    };

    const auto record = [&](const TrackInfo& track) {
        if (doomed.count(track.id) == 1)
            return;

        const bool sendsIn = std::ranges::any_of(track.sends, [&doomed](const SendInfo& send) {
            return doomed.count(send.destTrackId) == 1;
        });
        if (sendsIn || listensToADoomedTrack(track.audioInputDevice) ||
            listensToADoomedTrack(track.midiInputDevice))
            routing.tracks.push_back(
                {track.id, track.sends, track.audioInputDevice, track.midiInputDevice});

        collectSidechainsInto(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                              doomed, routing.sidechains);
    };

    for (const auto& track : tracks_)
        record(track);
    record(masterTrack_);

    return routing;
}

void TrackManager::restoreExternalRouting(const ExternalTrackRouting& routing) {
    for (const auto& entry : routing.tracks) {
        if (auto* track = getTrack(entry.trackId)) {
            // Whole, not by difference: the deletion only removed from these.
            track->sends = entry.sends;
            notifyTrackPropertyChanged(entry.trackId);
        }
        // Through the setters, so the engine follows the routing back.
        setTrackAudioInput(entry.trackId, entry.audioInputDevice);
        setTrackMidiInput(entry.trackId, entry.midiInputDevice);
    }

    for (const auto& entry : routing.sidechains) {
        if (entry.nodePath.getType() == ChainNodeType::Rack)
            setRackSidechainSource(entry.nodePath, entry.config.sourceTrackId, entry.config.type);
        else if (const auto deviceId = entry.nodePath.getDeviceId(); deviceId != INVALID_DEVICE_ID)
            setSidechainSource(deviceId, entry.config.sourceTrackId, entry.config.type);
    }
}

void TrackManager::restoreTrack(const TrackInfo& trackInfo, TrackRestorePosition position) {
    // Check if a track with this ID already exists
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [&trackInfo](const TrackInfo& t) { return t.id == trackInfo.id; });

    if (it != tracks_.end()) {
        DBG("Warning: Track with id=" << trackInfo.id << " already exists, skipping restore");
        return;
    }

    const int at = position.trackIndex < 0
                       ? static_cast<int>(tracks_.size())
                       : std::clamp(position.trackIndex, 0, static_cast<int>(tracks_.size()));
    auto inserted = tracks_.insert(tracks_.begin() + at, trackInfo);

    // Projects saved before the input-less-track invariant existed can carry
    // MIDI/audio input, monitoring, or record-arm on Aux/Group tracks
    // (createTrack used to seed every non-Aux track with "all"). Normalize on
    // restore rather than let stale on-disk state reintroduce it.
    inserted->normalizeForType();

    // Ensure nextTrackId_ is beyond any restored track IDs
    if (trackInfo.id >= nextTrackId_) {
        nextTrackId_ = trackInfo.id + 1;
    }

    // If track has a parent, add it back to parent's children -- where it stood,
    // not at the end. A group's order is its `childIds` order, so appending a
    // restored middle child reorders the group even when the project order is
    // right (#2229).
    if (trackInfo.hasParent()) {
        if (auto* parent = getTrack(trackInfo.parentId)) {
            auto& children = parent->childIds;
            if (std::find(children.begin(), children.end(), trackInfo.id) == children.end()) {
                const int amongSiblings =
                    position.siblingIndex < 0
                        ? static_cast<int>(children.size())
                        : std::clamp(position.siblingIndex, 0, static_cast<int>(children.size()));
                children.insert(children.begin() + amongSiblings, trackInfo.id);
            }
        }
    }

    // Register for MIDI input monitoring (no-op for input-less tracks).
    startMidiMonitoring(trackInfo, "all");

    notifyTracksChanged();
    DBG("Restored track: " << trackInfo.name << " (id=" << trackInfo.id << ")");
}

TrackId TrackManager::duplicateTrack(TrackId trackId, bool includeDevices) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it == tracks_.end()) {
        return INVALID_TRACK_ID;
    }

    // The chord track is a strict singleton - never duplicate it.
    if (it->type == TrackType::Chord) {
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

    // The shared walk. The duplicate's devices, racks, chains and pads all get
    // fresh ids: left alone, the copy's pad devices would share the original's
    // runtime, and its pad rack id would no longer be the one derived from its
    // device id, which is how every pad path is built (#2207, #2221).
    ChainIdRemap ids;
    reassignChainElementIds(newTrack.chain.fxChainElements, ids);
    remap.devices = std::move(ids.devices);
    remap.racks = std::move(ids.racks);
    remap.chains = std::move(ids.chains);
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

    // The copy is not a child track of anything. Its devices need no such reset:
    // a DeviceInfo no longer carries which child tracks its pairs drive, so a
    // copy inherits no ownership to begin with (#2220).
    newTrack.multiOutLink.reset();

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

    // Register for MIDI input monitoring (no-op for input-less tracks).
    startMidiMonitoring(newTrack, newTrack.midiInputDevice);

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

int TrackManager::getTrackSiblingPosition(TrackId trackId) const {
    const auto* track = getTrack(trackId);
    if (!track)
        return 0;

    const std::vector<TrackId>* siblings = nullptr;
    std::vector<TrackId> topLevel;
    if (track->hasParent()) {
        const auto* parent = getTrack(track->parentId);
        if (!parent)
            return 0;
        siblings = &parent->childIds;
    } else {
        topLevel = getTopLevelTracks();
        siblings = &topLevel;
    }

    for (size_t i = 0; i < siblings->size(); ++i)
        if ((*siblings)[i] == trackId)
            return static_cast<int>(i) + 1;
    return 0;
}

void TrackManager::moveTrackToPosition(TrackId trackId, int oneBasedPosition) {
    auto* track = getTrack(trackId);
    if (!track)
        return;

    // Grouped track: reorder within the parent's childIds (the display order for
    // group children). Compute the sibling that should follow it at the target
    // position and insert before it.
    if (track->hasParent()) {
        const auto* parent = getTrack(track->parentId);
        if (!parent)
            return;
        std::vector<TrackId> order = parent->childIds;
        const int count = static_cast<int>(order.size());
        if (count <= 1)
            return;
        const int pos = juce::jlimit(1, count, oneBasedPosition);
        order.erase(std::remove(order.begin(), order.end(), trackId), order.end());
        const TrackId before = (pos - 1 < static_cast<int>(order.size()))
                                   ? order[static_cast<size_t>(pos - 1)]
                                   : INVALID_TRACK_ID;
        moveChildWithinGroup(trackId, before);  // fires notifyTracksChanged()
        return;
    }

    // Top-level track (incl. a group header): reorder among top-level tracks.
    // Display is tree-derived, so only the header's position relative to other
    // top-level tracks in tracks_ matters; its children follow via childIds.
    auto topLevel = getTopLevelTracks();
    const int count = static_cast<int>(topLevel.size());
    if (count <= 1)
        return;
    const int pos = juce::jlimit(1, count, oneBasedPosition);

    std::vector<TrackId> desired;
    desired.reserve(topLevel.size());
    for (auto id : topLevel)
        if (id != trackId)
            desired.push_back(id);
    const TrackId anchor = (pos - 1 < static_cast<int>(desired.size()))
                               ? desired[static_cast<size_t>(pos - 1)]
                               : INVALID_TRACK_ID;

    auto self = std::find_if(tracks_.begin(), tracks_.end(),
                             [&](const TrackInfo& t) { return t.id == trackId; });
    if (self == tracks_.end())
        return;
    const TrackInfo info = *self;
    tracks_.erase(self);

    if (anchor == INVALID_TRACK_ID) {
        tracks_.push_back(info);
    } else {
        auto at = std::find_if(tracks_.begin(), tracks_.end(),
                               [&](const TrackInfo& t) { return t.id == anchor; });
        tracks_.insert(at, info);
    }
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

bool TrackManager::wouldCreateInputRoutingCycle(TrackId destTrackId, TrackId sourceTrackId) const {
    if (sourceTrackId == destTrackId)
        return true;

    // Walk the "track:" input chain upstream from the source track. Each track
    // has at most one track input (audio and MIDI inputs are mutually
    // exclusive), so this is a simple chain walk; the visited set terminates
    // the walk on any pre-existing loop.
    std::unordered_set<TrackId> visited;
    TrackId current = sourceTrackId;
    while (visited.insert(current).second) {
        const auto* track = getTrack(current);
        if (!track)
            return false;

        juce::String input;
        if (track->audioInputDevice.startsWith("track:"))
            input = track->audioInputDevice;
        else if (track->midiInputDevice.startsWith("track:"))
            input = track->midiInputDevice;
        else
            return false;

        current = input.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        if (current == destTrackId)
            return true;
    }
    return false;
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
        // Input-less tracks (Aux, Group) have no clip lane to record into, so
        // they can't be record-armed.
        if (!track->takesExternalInput())
            return;
        track->recordArmed = armed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackInputMonitor(TrackId trackId, InputMonitorMode mode) {
    if (auto* track = getTrack(trackId)) {
        // Input-less tracks (Aux, Group) take no external input, so input
        // monitoring does not apply to them.
        if (!track->takesExternalInput())
            return;
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

void TrackManager::setTrackMixerChannelWidth(TrackId trackId, int width) {
    if (auto* track = getTrack(trackId)) {
        const int clamped = juce::jlimit(0, 180, width);
        if (track->mixerChannelWidth == clamped)
            return;
        track->mixerChannelWidth = clamped;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackMixerFaderTopInset(TrackId trackId, int inset) {
    if (auto* track = getTrack(trackId)) {
        const int clamped = juce::jlimit(0, 400, inset);
        if (track->mixerFaderTopInset == clamped)
            return;
        track->mixerFaderTopInset = clamped;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setAudioEngine(AudioEngine* audioEngine) {
    audioEngine_ = audioEngine;

    // Sync existing tracks' MIDI routing (in case tracks were created before engine was set)
    // Only set up MidiBridge monitoring; TE-level MIDI routing is handled by
    // AudioBridge::updateMidiInputRouting() based on selection/arm state.
    if (audioEngine_) {
        for (const auto& track : tracks_) {
            if (!track.midiInputDevice.isEmpty())
                startMidiMonitoring(track, track.midiInputDevice);
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

    // Input-less tracks (Aux, Group) never receive external MIDI
    if (!track->takesExternalInput()) {
        DBG("Cannot set MIDI input on input-less track " << trackId);
        return;
    }

    DBG("TrackManager::setTrackMidiInput - trackId=" << trackId << " deviceId='" << deviceId
                                                     << "'");

    // Internal track routing: validate "track:" sources before touching the model
    if (deviceId.startsWith("track:")) {
        const TrackId sourceId =
            deviceId.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        if (!getTrack(sourceId)) {
            DBG("  -> Rejected: source track " << sourceId << " does not exist");
            return;
        }
        if (wouldCreateInputRoutingCycle(trackId, sourceId)) {
            DBG("  -> Rejected: routing track " << sourceId << " into track " << trackId
                                                << " would create an input routing cycle");
            return;
        }
    }

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

    // Forward to MidiBridge for MIDI activity monitoring (UI indicators).
    // MidiBridge is a hardware MIDI input callback — "track:" sources are
    // routed internally via MidiInputRouter, so clear any hardware routing.
    if (audioEngine_) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            if (deviceId.isEmpty() || deviceId.startsWith("track:")) {
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

    // The MIDI output selector is one exclusive choice: picking a hardware
    // device (or None) tears down any internal "MIDI To track" routing.
    clearMidiTrackListeners(trackId);
    // Re-fetch: clearMidiTrackListeners triggers notifyTrackPropertyChanged which
    // may cause listeners to modify the tracks_ vector, invalidating our pointer.
    track = getTrack(trackId);
    if (!track)
        return;

    // Update track state
    track->midiOutputDevice = deviceId;

    // Notify listeners (AudioBridge forwards to TrackController for TE routing)
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::routeMidiOutputToTrack(TrackId sourceTrackId, TrackId destTrackId) {
    DBG("TrackManager::routeMidiOutputToTrack - source=" << sourceTrackId
                                                         << " dest=" << destTrackId);

    // Validate everything up-front — a rejected route must change nothing.
    auto* source = getTrack(sourceTrackId);
    const auto* dest = getTrack(destTrackId);
    if (!source || !dest) {
        DBG("  -> Rejected: unknown source or destination track");
        return;
    }
    if (dest->type == TrackType::Aux) {
        DBG("  -> Rejected: aux tracks don't receive MIDI");
        return;
    }
    // Also rejects self-routing (source == dest counts as a cycle)
    if (wouldCreateInputRoutingCycle(destTrackId, sourceTrackId)) {
        DBG("  -> Rejected: routing track " << sourceTrackId << " into track " << destTrackId
                                            << " would create an input routing cycle");
        return;
    }

    // Single destination: clear every other track currently listening to the source
    clearMidiTrackListeners(sourceTrackId, destTrackId);

    // Route the source into the destination (input-owned edge on the destination)
    setTrackMidiInput(destTrackId, "track:" + juce::String(sourceTrackId));

    // The MIDI output selector is one exclusive choice — clear the source's
    // hardware MIDI output. Set the field directly (setTrackMidiOutput would
    // tear down the listener we just created) and notify so the source's
    // routing UI re-syncs its mirror view.
    // Re-fetch: the setters above notify listeners which may mutate tracks_.
    source = getTrack(sourceTrackId);
    if (!source)
        return;
    source->midiOutputDevice = "";
    notifyTrackPropertyChanged(sourceTrackId);
}

void TrackManager::clearMidiTrackListeners(TrackId sourceTrackId, TrackId excludeTrackId) {
    // Collect ids first: the setter notifies listeners, which may mutate tracks_.
    const juce::String trackInputId = "track:" + juce::String(sourceTrackId);
    std::vector<TrackId> listenerIds;
    for (const auto& t : tracks_) {
        if (t.id != excludeTrackId && t.midiInputDevice == trackInputId)
            listenerIds.push_back(t.id);
    }
    for (auto listenerId : listenerIds)
        setTrackMidiInput(listenerId, "");
}

void TrackManager::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    // Input-less tracks (Aux, Group) take no external input.
    if (!track->takesExternalInput()) {
        DBG("Cannot set audio input on input-less track " << trackId);
        return;
    }

    DBG("TrackManager::setTrackAudioInput - trackId=" << trackId << " deviceId='" << deviceId
                                                      << "'");

    // Internal track routing: validate "track:" sources before touching the model
    if (deviceId.startsWith("track:")) {
        const TrackId sourceId =
            deviceId.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        if (!getTrack(sourceId)) {
            DBG("  -> Rejected: source track " << sourceId << " does not exist");
            return;
        }
        if (wouldCreateInputRoutingCycle(trackId, sourceId)) {
            DBG("  -> Rejected: routing track " << sourceId << " into track " << trackId
                                                << " would create an input routing cycle");
            return;
        }
    }

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

std::vector<std::string> TrackManager::getChainSummary(TrackId trackId) const {
    std::vector<std::string> out;
    const auto* track = getTrack(trackId);
    if (track == nullptr)
        return out;

    auto add = [&out](const DeviceInfo& d) {
        // Only effect inserts -- the mixing chain. Skip instruments, MIDI
        // processors and analysis (transparent) devices.
        if (d.deviceType != DeviceType::Effect)
            return;
        juce::String s = d.name;
        if (d.bypassed)
            s += " (bypassed)";
        out.push_back(s.toStdString());
    };

    std::function<void(const std::vector<ChainElement>&)> walk =
        [&](const std::vector<ChainElement>& elements) {
            for (const auto& e : elements) {
                if (magda::isDevice(e))
                    add(magda::getDevice(e));
                else
                    for (const auto& chain : magda::getRack(e).chains)
                        walk(chain.elements);
            }
        };
    walk(track->chain.fxChainElements);
    for (const auto& pf : track->chain.postFxChainElements)
        add(pf.device);

    return out;
}

TrackManager::ExternalInstrumentRouting TrackManager::getExternalInstrumentRouting(
    TrackId trackId) const {
    ExternalInstrumentRouting routing;
    const auto* track = getTrack(trackId);
    if (track == nullptr)
        return routing;

    // Inserts can't live inside racks (canCreateDetached=false), so only the
    // top-level chain elements need checking.
    for (const auto& e : track->chain.fxChainElements) {
        if (!magda::isDevice(e))
            continue;
        const auto& d = magda::getDevice(e);
        if (!(d.isInstrument && daw::audio::internalPluginHasTag(d.pluginId, "external-insert")))
            continue;

        routing.present = true;
        // Mirror the live insert's chosen send/return devices so the track-level
        // selectors can display them. The plugin may not be resolvable yet
        // (path invalid during load); present stays true regardless.
        if (audioEngine_ != nullptr) {
            if (auto* bridge = audioEngine_->getAudioBridge()) {
                auto path = ChainNodePath::topLevelDevice(trackId, d.id);
                if (auto plugin = bridge->getPlugin(path)) {
                    if (auto* insert =
                            dynamic_cast<tracktion::engine::InsertPlugin*>(plugin.get())) {
                        routing.midiOut = insert->outputDevice.get();
                        routing.audioReturn = insert->inputDevice.get();
                    }
                }
            }
        }
        break;
    }
    return routing;
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

void TrackManager::seedSidechainModIfMissing(DeviceInfo& dev, const ChainNodePath& devicePath) {
    const auto* spec = daw::audio::findInternalPluginSpec(dev.pluginId);
    if (spec == nullptr || !daw::audio::internalPluginHasTag(*spec, "sidechain") ||
        spec->defaultModulationParamIndex < 0)
        return;
    if (!dev.mods.empty())
        return;

    ModInfo mod(0);
    mod.name = "Duck";
    mod.waveform = LFOWaveform::Custom;
    mod.curvePreset = CurvePreset::Custom;
    // The drawn curve is the audible gain envelope: 0 at note-on (fully
    // ducked) easing back up to 1 (full level) over one cycle - the classic
    // sidechain shape. Positive tension keeps it low early and steepens into
    // the recovery. One-shot holds the end value (full level) between notes.
    CurvePointData start;
    start.phase = 0.0f;
    start.value = 0.0f;
    start.tension = 1.0f;
    CurvePointData end;
    end.phase = 1.0f;
    end.value = 1.0f;
    mod.curvePoints = {start, end};
    // Applied output is (1 - curve): the engine receives duck amount, so an
    // inactive modifier (untriggered/gated forces output 0) means unity gain.
    mod.invertOutput = true;
    mod.triggerMode = LFOTriggerMode::MIDI;
    mod.tempoSync = true;
    mod.syncDivision = SyncDivision::Quarter;
    mod.oneShot = true;
    // Full negative depth on the duck output: gain = 1 - depth * (1 - curve),
    // i.e. at full depth the gain follows the drawn curve exactly. The
    // faceplate's depth control edits this link amount.
    mod.addLink(ControlTarget::pluginParam(devicePath, spec->defaultModulationParamIndex), -1.0f);
    dev.mods.push_back(mod);
}

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if (!track->canHostInstrument() && device.isInstrument) {
            DBG("Cannot add instrument '" << device.name << "' to track " << trackId
                                          << " (type=" << static_cast<int>(track->type)
                                          << " cannot host an instrument)");
            return INVALID_DEVICE_ID;
        }
        if (track->type == TrackType::Master &&
            (device.deviceType == DeviceType::MIDI ||
             daw::audio::isInternalMidiGeneratorPlugin(device.pluginId))) {
            DBG("Cannot add MIDI generator to master track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = prepareNewDevice(device);
        seedSidechainModIfMissing(newDevice, ChainNodePath::topLevelDevice(trackId, newDevice.id));
        track->chain.fxChainElements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        notifyDeviceAdded(ChainNodePath::topLevelDevice(trackId, newDevice.id), newDevice);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                             << trackId);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device,
                                        int insertIndex) {
    if (auto* track = getTrack(trackId)) {
        if (!track->canHostInstrument() && device.isInstrument) {
            DBG("Cannot add instrument '" << device.name << "' to track " << trackId
                                          << " (type=" << static_cast<int>(track->type)
                                          << " cannot host an instrument)");
            return INVALID_DEVICE_ID;
        }
        if (track->type == TrackType::Master &&
            (device.deviceType == DeviceType::MIDI ||
             daw::audio::isInternalMidiGeneratorPlugin(device.pluginId))) {
            DBG("Cannot add MIDI generator to master track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = prepareNewDevice(device);
        seedSidechainModIfMissing(newDevice, ChainNodePath::topLevelDevice(trackId, newDevice.id));

        // Clamp insert index to valid range
        int maxIndex = static_cast<int>(track->chain.fxChainElements.size());
        insertIndex = std::clamp(insertIndex, 0, maxIndex);

        // Insert at specified position
        track->chain.fxChainElements.insert(track->chain.fxChainElements.begin() + insertIndex,
                                            makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        notifyDeviceAdded(ChainNodePath::topLevelDevice(trackId, newDevice.id), newDevice);
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

    // The Sidechain device cannot work post-fader: device mods only sync for
    // main-chain devices, so its bundled duck modulator would never run (and
    // this path does not seed it). Reject instead of creating a dead insert.
    if (daw::audio::internalPluginHasTag(device.pluginId, "sidechain")) {
        DBG("Cannot add Sidechain device to post-fx chain");
        return INVALID_DEVICE_ID;
    }

    // Analysis devices (oscilloscope / spectrum) are unique per kind in post-fx:
    // the header toggles rely on a 1:1 mapping. Regular FX may repeat freely.
    if (daw::audio::isInternalAnalysisPlugin(device.pluginId) &&
        findPostFxDevice(trackId, device.pluginId) != INVALID_DEVICE_ID) {
        DBG("Post-fx already has analysis device " << device.pluginId << "; skipping duplicate");
        return INVALID_DEVICE_ID;
    }

    DeviceInfo newDevice = device;
    newDevice.id = nextPostFxDeviceId_++;
    applyCachedCapabilitiesToDevice(newDevice);
    if (daw::audio::isInternalAnalysisPlugin(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;

    auto& elements = track->chain.postFxChainElements;
    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(elements.size()));
    elements.insert(elements.begin() + insertIndex, PostFxChainElement{newDevice});
    enforcePostFxAnalysisDeviceOrder(elements);
    notifyTrackDevicesChanged(trackId);
    notifyDeviceAdded(ChainNodePath::postFxDevice(trackId, newDevice.id), newDevice);
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
    applyCachedCapabilitiesToDevice(newDevice);
    if (daw::audio::isInternalAnalysisPlugin(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;
    track->chain.mixerAnalysisElements.push_back(PostFxChainElement{newDevice});
    notifyTrackDevicesChanged(trackId);
    notifyDeviceAdded(ChainNodePath::mixerAnalysisDevice(trackId, newDevice.id), newDevice);
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
        if (bypassed)
            device->deltaSolo = false;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setDeviceBypassedByPath(const ChainNodePath& devicePath, bool bypassed) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->bypassed = bypassed;
        if (bypassed)
            device->deltaSolo = false;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

bool TrackManager::isChainEnabled(TrackId trackId) const {
    const auto* track = getTrack(trackId);
    return track == nullptr || track->chain.enabled;
}

bool TrackManager::isDeviceEffectivelyEnabled(const ChainNodePath& devicePath,
                                              const DeviceInfo& device) const {
    if (device.bypassed)
        return false;
    // Post-FX and mixer-analysis devices sit outside the insert chain, so the
    // chain power does not gate them.
    if (devicePath.isPostFx() || devicePath.isMixerAnalysis())
        return true;
    return isChainEnabled(devicePath.trackId);
}

void TrackManager::setChainEnabled(TrackId trackId, bool enabled) {
    auto* track = getTrack(trackId);
    if (!track || track->chain.enabled == enabled)
        return;
    track->chain.enabled = enabled;

    // Re-apply effective enablement on every insert-chain device through the
    // regular device sync path (AudioBridge::devicePropertyChanged applies the
    // chain gate on top of each device's own bypassed flag). The flags
    // themselves are untouched, so per-device bypass survives an off/on cycle.
    std::vector<ChainNodePath> devicePaths;
    collectChainDevicePaths(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                            devicePaths);
    for (const auto& devicePath : devicePaths)
        notifyDevicePropertyChanged(devicePath);
    notifyTrackDevicesChanged(trackId);
}

void TrackManager::setPostFxPostFader(TrackId trackId, bool postFader) {
    auto* track = getTrack(trackId);
    if (!track || track->chain.postFxPostFader == postFader)
        return;
    track->chain.postFxPostFader = postFader;

    // A devices-changed notification rather than a property one: this moves
    // plugins across the fader, which is a change to the shape of the chain and
    // is what PluginManager's reorder pass keys off.
    notifyTrackDevicesChanged(trackId);
}

bool TrackManager::isPostFxPostFader(TrackId trackId) const {
    const auto* track = getTrack(trackId);
    return track == nullptr ? true : track->chain.postFxPostFader;
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

void TrackManager::clearSelectionsUnderChain(const std::vector<ChainElement>& elements,
                                             const ChainNodePath& chainPath) {
    auto& selection = SelectionManager::getInstance();
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            selection.clearSelectionForDeletedChainNode(
                chainPath.withDevice(magda::getDevice(element).id));
            continue;
        }

        if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);
            clearSelectionsUnderRack(rack, chainPath.withRack(rack.id));
        }
    }
}

void TrackManager::clearSelectionsUnderRack(const RackInfo& rack, const ChainNodePath& rackPath) {
    auto& selection = SelectionManager::getInstance();
    for (const auto& chain : rack.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        clearSelectionsUnderChain(chain.elements, chainPath);
        selection.clearSelectionForDeletedChainNode(chainPath);
    }
    selection.clearSelectionForDeletedChainNode(rackPath);
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
            clearSelectionsUnderRack(magda::getRack(*it), ChainNodePath::rack(trackId, rackId));
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
        if (bypassed)
            rack->deltaSolo = false;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) {
    if (auto* rack = getRackByPath(rackPath)) {
        rack->bypassed = bypassed;
        if (bypassed)
            rack->deltaSolo = false;
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackDeltaSoloByPath(const ChainNodePath& rackPath, bool deltaSolo) {
    if (auto* rack = getRackByPath(rackPath)) {
        rack->deltaSolo = deltaSolo;
        if (deltaSolo)
            rack->bypassed = false;
        notifyTrackDevicesChanged(rackPath.trackId);
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

namespace {

/// The rack @p rackId names among @p elements.
///
/// Allocated racks only. A Drum Grid's pads are a rack too, but their address
/// is the grid's own DeviceId, and rack ids and device ids come out of counters
/// that both start at 1, so a Rack step cannot tell the two apart. Nothing
/// resolves a pad through one: `TrackManager::getPads()` reaches them through
/// the device that owns them, which is unambiguous (#2207).
RackInfo* findRackAmong(std::vector<ChainElement>& elements, RackId rackId) {
    for (auto& element : elements)
        if (magda::isRack(element) && magda::getRack(element).id == rackId)
            return &magda::getRack(element);
    return nullptr;
}

const RackInfo* findRackAmong(const std::vector<ChainElement>& elements, RackId rackId) {
    return findRackAmong(const_cast<std::vector<ChainElement>&>(elements), rackId);
}

}  // namespace

RackInfo* TrackManager::getRackInPadByPath(const ChainNodePath& rackPath) {
    if (!rackPath.isPadOwned() || rackPath.steps.empty())
        return nullptr;

    // `PadRack(grid)` on its own names the grid's own pad rack, which hangs off
    // the device rather than sitting in a chain.
    if (rackPath.steps.size() == 1) {
        const auto gridPath = findDevicePath(rackPath.getPadOwnerDeviceId());
        return gridPath.isValid() && gridPath.trackId == rackPath.trackId ? getPads(gridPath)
                                                                          : nullptr;
    }

    // A path ending on a chain or a device names the rack that encloses it,
    // which is the #2057 leniency the rack walk already has. The node itself has
    // to resolve first, so a route that does not exist answers nothing rather
    // than answering with whatever it passed through.
    const auto& last = rackPath.steps.back();
    if (isChainStep(last.type))
        return getChainInPadByPath(rackPath) != nullptr ? getRackInPadByPath(rackPath.parent())
                                                        : nullptr;
    if (last.type == ChainStepType::Device)
        return getDeviceInPadByPath(rackPath) != nullptr ? getRackInPadByPath(rackPath.parent())
                                                         : nullptr;

    if (last.type != ChainStepType::Rack)
        return nullptr;

    // Anything else ends on an allocated rack, sitting in a chain the pad route
    // reaches: a pad's chain holds racks like any other chain does.
    auto* chain = getChainInPadByPath(rackPath.parent());
    if (chain == nullptr)
        return nullptr;

    for (auto& element : chain->elements)
        if (magda::isRack(element) && magda::getRack(element).id == last.id)
            return &magda::getRack(element);

    return nullptr;
}

RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) {
    // A pad-owned address goes to the pad route, the way `getChainByPath()`
    // already sends one there. The walk below searches chain elements for a
    // rack id, and a `PadRack` step carries neither (#2229).
    if (rackPath.isPadOwned())
        return getRackInPadByPath(rackPath);

    auto* track = getTrack(rackPath.trackId);
    if (!track) {
        return nullptr;
    }

    // Track-level and legacy top-level-device paths cannot name an enclosing
    // rack. Reject contradictory paths too (for example, a top-level device id
    // combined with rack steps) instead of silently ignoring one representation.
    if (rackPath.isTrackLevel || rackPath.topLevelDeviceId != INVALID_DEVICE_ID)
        return nullptr;

    RackInfo* currentRack = nullptr;
    ChainInfo* currentChain = nullptr;

    // A step that resolves to nothing ends the walk. This used to leave the
    // previous `currentRack` in place and carry on, which made a broken path
    // resolve to whatever it had passed through — so `outer > missingChain >
    // missingRack` answered with `outer`. Worse, a missed Chain step left
    // `currentChain` null, so the *next* Rack step searched the track's
    // top-level list again and `outer > missingChain > someOtherTopLevelRack`
    // answered with that unrelated rack.
    //
    // Every path-based rack mutator resolves through here, so that was a write
    // landing silently on a node the caller never named. Failing closed is the
    // only answer that cannot do that.
    //
    // The structure has to hold as well as the ids. Racks contain chains and
    // chains contain racks, so a route alternates `Rack > Chain > Rack > Chain`;
    // a step that cannot follow the one before it describes a route the model
    // has no way to express. Checking only that each id resolves is not enough,
    // because two consecutive steps of the same kind then move sideways through
    // the tree using ids that all genuinely exist:
    //
    //   rack(A).withRack(B)            — B is a *sibling* top-level rack, and
    //                                    with `currentChain` null the second
    //                                    Rack step searched the track list
    //                                    again and found it.
    //   rack(R).withChain(c1).withChain(c2)
    //                                  — c2 is a sibling chain of c1 in the
    //                                    same rack, reached by a route that
    //                                    never went through anything.
    //
    // Both resolved, and every path-based rack mutator would then write to a
    // node on the other side of the tree from the one the caller named.
    //
    // Deliberately unchanged: a path that resolves *completely* but ends on a
    // Chain or nested Device step still returns the enclosing rack rather than
    // nothing. That is a terminal-type leniency on a route that does exist,
    // which is a different question — #2057. A Device step therefore still has
    // to name a real device in the current chain before this lookup may return
    // that enclosing rack.
    for (std::size_t index = 0; index < rackPath.steps.size(); ++index) {
        const auto& step = rackPath.steps[index];
        const bool isLast = index + 1 == rackPath.steps.size();

        switch (step.type) {
            // A PadRack is answered above, before the walk starts: it is always
            // the first step of a pad-owned path, so one reaching here would be
            // a pad step in a position the model cannot express.
            case ChainStepType::PadRack:
                return nullptr;
            case ChainStepType::Rack: {
                // Valid at track level, or immediately inside a chain. A Rack
                // straight after a Rack is the sideways move above.
                if (currentRack != nullptr && currentChain == nullptr)
                    return nullptr;

                // Top-level racks live in the track's own FX list; a nested one
                // lives in the chain the previous step reached.
                auto& elements =
                    currentChain != nullptr ? currentChain->elements : track->chain.fxChainElements;
                RackInfo* found = findRackAmong(elements, step.id);
                if (found == nullptr)
                    return nullptr;
                currentRack = found;
                currentChain = nullptr;  // Reset chain context
                break;
            }
            case ChainStepType::PadChain:
            case ChainStepType::Chain: {
                // Only ever directly inside a rack. A Chain after a Chain would
                // hop between siblings.
                if (currentRack == nullptr || currentChain != nullptr)
                    return nullptr;

                ChainInfo* found = nullptr;
                for (auto& chain : currentRack->chains) {
                    if (chain.id == step.id) {
                        found = &chain;
                        break;
                    }
                }
                if (found == nullptr)
                    return nullptr;
                currentChain = found;
                break;
            }
            case ChainStepType::Device: {
                // A nested device is a leaf directly inside the current chain.
                // Validate both the parent and the id before preserving the
                // #2057 behavior of returning its enclosing rack.
                if (!isLast || currentChain == nullptr)
                    return nullptr;
                const auto found = std::find_if(
                    currentChain->elements.begin(), currentChain->elements.end(),
                    [&step](const ChainElement& element) {
                        return magda::isDevice(element) && magda::getDevice(element).id == step.id;
                    });
                if (found == currentChain->elements.end())
                    return nullptr;
                break;
            }
            case ChainStepType::Segment:
                // Explicit segments select the flat post-fx or mixer-analysis
                // lists (the main FX segment is implicit). None can contain a
                // rack, so a segmented path has no answer in this resolver.
                return nullptr;
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
            const auto chainPath = ChainNodePath::rack(trackId, rackId).withChain(chainId);
            clearSelectionsUnderChain(it->elements, chainPath);
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(chainPath);
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
            clearSelectionsUnderChain(it->elements, chainPath);
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(chainPath);
            chains.erase(it);
            notifyTrackDevicesChanged(chainPath.trackId);
        }
    } else {
        DBG("removeChainByPath FAILED - rack not found via path!");
    }
}

bool TrackManager::insertChainIntoRackByPath(const ChainNodePath& rackPath, ChainInfo chain,
                                             int index) {
    auto* rack = getRackByPath(rackPath);
    if (rack == nullptr || chain.id == INVALID_CHAIN_ID)
        return false;

    index = std::clamp(index, 0, static_cast<int>(rack->chains.size()));
    rack->chains.insert(rack->chains.begin() + index, std::move(chain));
    notifyTrackDevicesChanged(rackPath.trackId);
    return true;
}

ChainInfo* TrackManager::getChainByPath(const ChainNodePath& chainPath) {
    // A pad chain hangs off a device rather than off a rack in the chain tree,
    // and its typed address says so, so it needs no walk (#2219).
    if (chainPath.isPadOwned())
        return getChainInPadByPath(chainPath);

    if (chainPath.steps.empty() || chainPath.steps.back().type != ChainStepType::Chain)
        return nullptr;

    // A chain hangs directly off a rack, so the step before it has to be one.
    // Checking the prefix through `getRackByPath` is not enough on its own: for
    // `rack > chain1 > chain2` the prefix `rack > chain1` is a perfectly legal
    // route, and the search below would then find `chain2` sitting beside
    // `chain1` in that same rack — a sibling reached by a path that never
    // descended into anything.
    if (chainPath.steps.size() < 2 ||
        chainPath.steps[chainPath.steps.size() - 2].type != ChainStepType::Rack)
        return nullptr;

    const ChainId chainId = chainPath.steps.back().id;

    // Parent rack is the path with the trailing Chain step removed.
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i + 1 < chainPath.steps.size(); ++i)
        rackPath.steps.push_back(chainPath.steps[i]);

    if (auto* rack = getRackByPath(rackPath)) {
        for (auto& chain : rack->chains) {
            if (chain.id == chainId)
                return &chain;
        }
    }
    return nullptr;
}

const ChainInfo* TrackManager::getChainByPath(const ChainNodePath& chainPath) const {
    return const_cast<TrackManager*>(this)->getChainByPath(chainPath);
}

void TrackManager::setChainMuted(const ChainNodePath& chainPath, bool muted) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->muted = muted;
        notifyTrackDevicesChanged(chainPath.trackId);
    }
}

void TrackManager::setChainBypassed(const ChainNodePath& chainPath, bool bypassed) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->bypassed = bypassed;
        notifyTrackDevicesChanged(chainPath.trackId);
    }
}

void TrackManager::setChainSolo(const ChainNodePath& chainPath, bool solo) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->solo = solo;
        notifyTrackDevicesChanged(chainPath.trackId);
    }
}

void TrackManager::setChainVolume(const ChainNodePath& chainPath, float volume) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->volume = juce::jlimit(-60.0f, 6.0f, volume);
        notifyTrackPropertyChanged(chainPath.trackId);
    }
}

void TrackManager::setChainPan(const ChainNodePath& chainPath, float pan) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackPropertyChanged(chainPath.trackId);
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

void TrackManager::setChainOutput(const ChainNodePath& chainPath, int outputIndex) {
    if (auto* chain = getChainByPath(chainPath)) {
        chain->outputIndex = outputIndex;
        notifyTrackDevicesChanged(chainPath.trackId);
    }
}

void TrackManager::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    setChainOutput(ChainNodePath::chain(trackId, rackId, chainId), outputIndex);
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

void TrackManager::setChainName(const ChainNodePath& chainPath, const juce::String& name) {
    if (auto* chain = getChainByPath(chainPath)) {
        auto trimmed = name.trim();
        if (trimmed.isEmpty() || trimmed == chain->name)
            return;
        chain->name = trimmed;
        notifyTrackPropertyChanged(chainPath.trackId);
    }
}

void TrackManager::setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                                const juce::String& name) {
    setChainName(ChainNodePath::chain(trackId, rackId, chainId), name);
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
            case ChainStepType::PadRack:
            case ChainStepType::Rack: {
                // A top-level rack lives in the track's own list, a nested one
                // in the chain the previous step reached. A PadRack names a
                // device, so it finds nothing here and contributes no name,
                // exactly as the untyped spelling did (#2219).
                const auto& elements =
                    currentChain != nullptr ? currentChain->elements : track->chain.fxChainElements;
                if (const auto* found = findRackAmong(elements, step.id)) {
                    currentRack = found;
                    currentChain = nullptr;  // Reset chain context
                    pathNames.add(currentRack->name);
                }
                break;
            }
            case ChainStepType::PadChain:
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
            // A pad device's id comes out of the same counter, so a Drum Grid's
            // pads have to be scanned or the next device added anywhere on the
            // project reuses one of theirs (#2207).
            if (device.pads)
                for (const auto& pad : device.pads->chains)
                    for (const auto& padElement : pad.elements)
                        if (magda::isDevice(padElement))
                            maxFxDeviceId =
                                std::max(maxFxDeviceId, magda::getDevice(padElement).id);
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
    // While a batch is open, coalesce: record the change and fire once when the
    // outermost scope closes. A multi-track fan-out (group/mute/fx across N
    // tracks) would otherwise rebuild every track/mixer panel N times.
    if (batchDepth_ > 0) {
        tracksChangedPending_ = true;
        return;
    }
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->tracksChanged();
    }
}

void TrackManager::beginBatch() {
    ++batchDepth_;
}

void TrackManager::endBatch() {
    if (batchDepth_ == 0)
        return;
    if (--batchDepth_ == 0 && tracksChangedPending_) {
        tracksChangedPending_ = false;
        notifyTracksChanged();
    }
}

TrackManager::ScopedListenerMuteForTests::ScopedListenerMuteForTests() {
    auto& manager = TrackManager::getInstance();
    savedListeners_ = std::move(manager.listeners_);
    manager.listeners_.clear();
}

TrackManager::ScopedListenerMuteForTests::~ScopedListenerMuteForTests() {
    auto& manager = TrackManager::getInstance();
    manager.listeners_ = std::move(savedListeners_);
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

void TrackManager::notifyDeviceAdded(const ChainNodePath& devicePath, const DeviceInfo& device) {
    ScopedNotifyGuard guard(*this);
    for (size_t i = 0; i < listeners_.size(); ++i) {
        if (listeners_[i])
            listeners_[i]->deviceAdded(devicePath, device);
    }
}

DeviceInfo TrackManager::prepareNewDevice(const DeviceInfo& device) {
    DeviceInfo newDevice = device;
    newDevice.id = nextFxDeviceId_++;
    rekeyPads(newDevice);
    applyCachedCapabilitiesToDevice(newDevice);
    stampDefaultKitIfMissing(newDevice);
    if (daw::audio::isInternalAnalysisPlugin(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;
    return newDevice;
}

void TrackManager::rekeyPads(DeviceInfo& device, std::map<DeviceId, DeviceId>* remap) {
    if (!device.pads)
        return;

    // Both the pad rack's id and its devices' are DeviceIds in disguise, so a
    // copied Drum Grid that kept them would key the ops of the one it was
    // copied from: the plan would emit two devices onto one op and the executor
    // would run whichever it saw last (#2207).
    stampPadRackId(device);

    for (auto& pad : device.pads->chains) {
        for (auto& element : pad.elements) {
            if (!magda::isDevice(element))
                continue;

            auto& padDevice = magda::getDevice(element);
            const auto oldId = padDevice.id;
            padDevice.id = allocateDeviceId();

            // Reported so a caller that also rewrites paths can follow the pad
            // devices, which is what keeps a macro or a mod on the grid pointing
            // at the pad it was linked to.
            if (remap != nullptr && oldId != INVALID_DEVICE_ID)
                (*remap)[oldId] = padDevice.id;
        }
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
