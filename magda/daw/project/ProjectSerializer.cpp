#include "ProjectSerializer.hpp"

#include <juce_data_structures/juce_data_structures.h>

#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/SelectionManager.hpp"
#include "../core/TrackManager.hpp"
#include "../core/ViewModeState.hpp"

namespace magda {

// ============================================================================
// File I/O with gzip compression
// ============================================================================

bool ProjectSerializer::saveToFile(const juce::File& file, const ProjectInfo& info) {
    try {
        // Serialize to JSON
        auto json = serializeProject(info);

        // Convert to pretty-printed string
        juce::String jsonString = juce::JSON::toString(json, true);

        // Use temporary file for atomic/crash-safe writing
        // Write to temp file first, then atomically replace destination
        juce::TemporaryFile tempFile(file);
        auto tempFileHandle = tempFile.getFile();

        // Write with gzip compression to temp file
        juce::FileOutputStream outputStream(tempFileHandle);
        if (!outputStream.openedOk()) {
            lastError_ =
                "Failed to open temporary file for writing: " + tempFileHandle.getFullPathName();
            return false;
        }

        juce::GZIPCompressorOutputStream gzipStream(outputStream, 9);  // Max compression
        // Write plain UTF-8 JSON text (no JUCE binary length prefix)
        gzipStream.writeText(jsonString, false, false, nullptr);
        gzipStream.flush();
        outputStream.flush();

        // Atomically replace destination with temp file
        // This ensures the original file is only replaced if write succeeds completely
        if (!tempFile.overwriteTargetFileWithTemporary()) {
            lastError_ = "Failed to replace target file with temporary file";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        lastError_ = "Exception while saving: " + juce::String(e.what());
        return false;
    } catch (...) {
        lastError_ = "Unknown exception while saving";
        return false;
    }
}

bool ProjectSerializer::loadFromFile(const juce::File& file, ProjectInfo& outInfo) {
    StagedProjectData staged;
    if (!loadAndStage(file, staged))
        return false;

    outInfo = staged.info;
    commitStaged(staged);
    return true;
}

bool ProjectSerializer::loadAndStage(const juce::File& file, StagedProjectData& outData) {
    try {
        // Check file exists
        if (!file.existsAsFile()) {
            lastError_ = "File does not exist: " + file.getFullPathName();
            return false;
        }

        // Read with gzip decompression
        juce::FileInputStream inputStream(file);
        if (!inputStream.openedOk()) {
            lastError_ = "Failed to open file for reading: " + file.getFullPathName();
            return false;
        }

        juce::GZIPDecompressorInputStream gzipStream(inputStream);
        juce::String jsonString = gzipStream.readEntireStreamAsString();

        // Parse JSON
        auto json = juce::JSON::parse(jsonString);
        if (json.isVoid()) {
            lastError_ = "Failed to parse JSON";
            return false;
        }

        // Deserialize project metadata
        if (!json.isObject()) {
            lastError_ = "Invalid project JSON: not an object";
            return false;
        }

        auto* obj = json.getDynamicObject();
        if (obj == nullptr) {
            lastError_ = "Invalid project JSON: null object";
            return false;
        }

        // Version check
        outData.info.version = obj->getProperty("magdaVersion").toString();
        if (outData.info.version.isEmpty()) {
            lastError_ = "Missing magdaVersion field";
            return false;
        }

        // Parse timestamp
        juce::String timeStr = obj->getProperty("lastModified").toString();
        if (timeStr.isNotEmpty()) {
            outData.info.lastModified = juce::Time::fromISO8601(timeStr);
        }

        // Parse project settings
        auto projectVar = obj->getProperty("project");
        if (!projectVar.isObject()) {
            lastError_ = "Missing or invalid project settings";
            return false;
        }

        auto* projectObj = projectVar.getDynamicObject();
        outData.info.name = projectObj->getProperty("name").toString();
        outData.info.tempo = projectObj->getProperty("tempo");

        // Time signature
        auto timeSigVar = projectObj->getProperty("timeSignature");
        if (timeSigVar.isArray()) {
            auto* arr = timeSigVar.getArray();
            if (arr->size() >= 2) {
                outData.info.timeSignatureNumerator = (*arr)[0];
                outData.info.timeSignatureDenominator = (*arr)[1];
            }
        }

        outData.info.projectLength = projectObj->getProperty("projectLength");

        // Loop settings
        auto loopVar = projectObj->getProperty("loop");
        if (loopVar.isObject()) {
            auto* loopObj = loopVar.getDynamicObject();
            outData.info.loopEnabled = loopObj->getProperty("enabled");
            outData.info.loopStartBeats = loopObj->getProperty("startBeats");
            outData.info.loopEndBeats = loopObj->getProperty("endBeats");
        }

        // Stage tracks, clips, and automation
        if (!deserializeTracksToStaging(obj->getProperty("tracks"), outData.tracks)) {
            return false;
        }

        if (!deserializeClipsToStaging(obj->getProperty("clips"), outData.clips,
                                       outData.info.tempo)) {
            return false;
        }

        if (!deserializeAutomationToStaging(obj->getProperty("automation"), outData.automationLanes,
                                            outData.automationClips)) {
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        lastError_ = "Exception while loading: " + juce::String(e.what());
        return false;
    } catch (...) {
        lastError_ = "Unknown exception while loading";
        return false;
    }
}

void ProjectSerializer::commitStaged(StagedProjectData& data) {
    commitStagedData(data.tracks, data.clips, data.automationLanes, data.automationClips);
}

// ============================================================================
// Project-level serialization
// ============================================================================

juce::var ProjectSerializer::serializeProject(const ProjectInfo& info) {
    auto* obj = new juce::DynamicObject();

    // Version and metadata
    obj->setProperty("magdaVersion", info.version);
    obj->setProperty("lastModified", info.lastModified.toISO8601(true));

    // Project settings
    auto* projectObj = new juce::DynamicObject();
    projectObj->setProperty("name", info.name);
    projectObj->setProperty("tempo", info.tempo);

    juce::Array<juce::var> timeSigArray;
    timeSigArray.add(info.timeSignatureNumerator);
    timeSigArray.add(info.timeSignatureDenominator);
    projectObj->setProperty("timeSignature", juce::var(timeSigArray));

    projectObj->setProperty("projectLength", info.projectLength);

    // Loop settings
    auto* loopObj = new juce::DynamicObject();
    loopObj->setProperty("enabled", info.loopEnabled);
    loopObj->setProperty("startBeats", info.loopStartBeats);
    loopObj->setProperty("endBeats", info.loopEndBeats);
    projectObj->setProperty("loop", juce::var(loopObj));

    obj->setProperty("project", juce::var(projectObj));

    // Serialize tracks, clips, and automation
    obj->setProperty("tracks", serializeTracks());
    obj->setProperty("clips", serializeClips());
    obj->setProperty("automation", serializeAutomation());

    return juce::var(obj);
}

bool ProjectSerializer::deserializeProject(const juce::var& json, ProjectInfo& outInfo) {
    if (!json.isObject()) {
        lastError_ = "Invalid project JSON: not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();
    if (obj == nullptr) {
        lastError_ = "Invalid project JSON: null object";
        return false;
    }

    // Version check
    outInfo.version = obj->getProperty("magdaVersion").toString();
    if (outInfo.version.isEmpty()) {
        lastError_ = "Missing magdaVersion field";
        return false;
    }

    // Parse timestamp
    juce::String timeStr = obj->getProperty("lastModified").toString();
    if (timeStr.isNotEmpty()) {
        outInfo.lastModified = juce::Time::fromISO8601(timeStr);
    }

    // Parse project settings
    auto projectVar = obj->getProperty("project");
    if (!projectVar.isObject()) {
        lastError_ = "Missing or invalid project settings";
        return false;
    }

    auto* projectObj = projectVar.getDynamicObject();
    outInfo.name = projectObj->getProperty("name").toString();
    outInfo.tempo = projectObj->getProperty("tempo");

    // Time signature
    auto timeSigVar = projectObj->getProperty("timeSignature");
    if (timeSigVar.isArray()) {
        auto* arr = timeSigVar.getArray();
        if (arr->size() >= 2) {
            outInfo.timeSignatureNumerator = (*arr)[0];
            outInfo.timeSignatureDenominator = (*arr)[1];
        }
    }

    outInfo.projectLength = projectObj->getProperty("projectLength");

    // Loop settings
    auto loopVar = projectObj->getProperty("loop");
    if (loopVar.isObject()) {
        auto* loopObj = loopVar.getDynamicObject();
        outInfo.loopEnabled = loopObj->getProperty("enabled");
        outInfo.loopStartBeats = loopObj->getProperty("startBeats");
        outInfo.loopEndBeats = loopObj->getProperty("endBeats");
    }

    // ATOMIC DESERIALIZATION: Validate and stage ALL components before modifying any state.
    // This ensures that if any component fails to deserialize, we don't leave the project
    // in a partially-loaded, inconsistent state.

    // Stage 1: Deserialize all components into temporary collections (validation phase)
    std::vector<TrackInfo> stagedTracks;
    std::vector<ClipInfo> stagedClips;
    std::vector<AutomationLaneInfo> stagedAutomation;
    std::vector<AutomationClipInfo> stagedAutomationClips;

    if (!deserializeTracksToStaging(obj->getProperty("tracks"), stagedTracks)) {
        return false;  // Failed - no state modified
    }

    if (!deserializeClipsToStaging(obj->getProperty("clips"), stagedClips, outInfo.tempo)) {
        return false;  // Failed - no state modified
    }

    if (!deserializeAutomationToStaging(obj->getProperty("automation"), stagedAutomation,
                                        stagedAutomationClips)) {
        return false;  // Failed - no state modified
    }

    // Stage 2: All components validated successfully - now commit to managers atomically
    commitStagedData(stagedTracks, stagedClips, stagedAutomation, stagedAutomationClips);

    return true;
}

// ============================================================================
// Atomic commit of staged deserialization data
// ============================================================================

void ProjectSerializer::commitStagedData(std::vector<TrackInfo>& stagedTracks,
                                         std::vector<ClipInfo>& stagedClips,
                                         std::vector<AutomationLaneInfo>& stagedAutomation,
                                         std::vector<AutomationClipInfo>& stagedAutomationClips) {
    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();
    auto& automationManager = AutomationManager::getInstance();

    // Clear selection state before clearing managers to ensure all listeners
    // are properly notified (prevents stale selection after project load)
    SelectionManager::getInstance().clearSelection();

    // Clear all existing data from managers
    trackManager.clearAllTracks();
    clipManager.clearAllClips();
    automationManager.clearAll();

    // Restore tracks
    for (auto& track : stagedTracks) {
        trackManager.restoreTrack(track);
    }

    // After all tracks are restored, ensure TrackManager ID counters
    // (track/device/rack/chain) are updated to avoid ID collisions.
    trackManager.refreshIdCountersFromTracks();

    // Restore clips
    for (auto& clip : stagedClips) {
        clipManager.restoreClip(clip);
    }

    // Restore automation lanes
    for (auto& lane : stagedAutomation) {
        automationManager.restoreLane(lane);
    }

    // Restore automation clips
    for (auto& clip : stagedAutomationClips) {
        automationManager.restoreClip(clip);
    }

    // Update automation ID counters to avoid collisions
    automationManager.refreshIdCountersFromLanes();

    // Select the first track so the UI has a valid selection after load
    if (!stagedTracks.empty()) {
        SelectionManager::getInstance().selectTrack(stagedTracks[0].id);
    }
}

// ============================================================================
// Component-level serialization
// ============================================================================

juce::var ProjectSerializer::serializeTracks() {
    juce::Array<juce::var> tracksArray;

    auto& trackManager = TrackManager::getInstance();
    for (const auto& track : trackManager.getTracks()) {
        tracksArray.add(serializeTrackInfo(track));
    }

    return juce::var(tracksArray);
}

juce::var ProjectSerializer::serializeClips() {
    juce::Array<juce::var> clipsArray;

    auto& clipManager = ClipManager::getInstance();
    for (const auto& clip : clipManager.getClips()) {
        clipsArray.add(serializeClipInfo(clip));
    }

    return juce::var(clipsArray);
}

juce::var ProjectSerializer::serializeAutomation() {
    auto& automationManager = AutomationManager::getInstance();

    juce::Array<juce::var> lanesArray;
    for (const auto& lane : automationManager.getLanes()) {
        lanesArray.add(serializeAutomationLaneInfo(lane));
    }

    juce::Array<juce::var> clipsArray;
    for (const auto& clip : automationManager.getClips()) {
        clipsArray.add(serializeAutomationClipInfo(clip));
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty("lanes", juce::var(lanesArray));
    obj->setProperty("clips", juce::var(clipsArray));

    return juce::var(obj);
}

// ============================================================================
// Component-level deserialization
// ============================================================================

bool ProjectSerializer::deserializeTracksToStaging(const juce::var& json,
                                                   std::vector<TrackInfo>& outTracks) {
    if (!json.isArray()) {
        lastError_ = "Tracks data is not an array";
        return false;
    }

    auto* arr = json.getArray();
    outTracks.clear();
    outTracks.reserve(arr->size());

    // Deserialize all tracks into staging vector (validation phase)
    for (const auto& trackVar : *arr) {
        TrackInfo track;
        if (!deserializeTrackInfo(trackVar, track)) {
            return false;  // Failed - staging vector discarded
        }
        outTracks.push_back(std::move(track));
    }

    return true;
}

bool ProjectSerializer::deserializeClipsToStaging(const juce::var& json,
                                                  std::vector<ClipInfo>& outClips,
                                                  double projectTempo) {
    if (!json.isArray()) {
        lastError_ = "Clips data is not an array";
        return false;
    }

    auto* arr = json.getArray();
    outClips.clear();
    outClips.reserve(arr->size());

    // Deserialize all clips into staging vector (validation phase)
    for (const auto& clipVar : *arr) {
        ClipInfo clip;
        if (!deserializeClipInfo(clipVar, clip, projectTempo)) {
            return false;  // Failed - staging vector discarded
        }
        outClips.push_back(std::move(clip));
    }

    return true;
}

bool ProjectSerializer::deserializeAutomationToStaging(const juce::var& json,
                                                       std::vector<AutomationLaneInfo>& outLanes,
                                                       std::vector<AutomationClipInfo>& outClips) {
    // Handle missing automation key gracefully for backward compatibility.
    // Older project files created before automation support won't have this key.
    if (json.isVoid()) {
        outLanes.clear();
        outClips.clear();
        return true;
    }

    // New format: object with "lanes" and "clips" arrays
    if (json.isObject()) {
        auto* obj = json.getDynamicObject();
        if (obj == nullptr) {
            lastError_ = "Automation data object is invalid";
            return false;
        }

        // Deserialize lanes
        auto lanesVar = obj->getProperty("lanes");
        if (lanesVar.isArray()) {
            auto* lanesArr = lanesVar.getArray();
            outLanes.clear();
            outLanes.reserve(lanesArr->size());
            for (const auto& laneVar : *lanesArr) {
                AutomationLaneInfo lane;
                if (!deserializeAutomationLaneInfo(laneVar, lane)) {
                    return false;
                }
                outLanes.push_back(std::move(lane));
            }
        }

        // Deserialize clips
        auto clipsVar = obj->getProperty("clips");
        if (clipsVar.isArray()) {
            auto* clipsArr = clipsVar.getArray();
            outClips.clear();
            outClips.reserve(clipsArr->size());
            for (const auto& clipVar : *clipsArr) {
                AutomationClipInfo clip;
                if (!deserializeAutomationClipInfo(clipVar, clip)) {
                    return false;
                }
                outClips.push_back(std::move(clip));
            }
        }

        return true;
    }

    // Legacy format: plain array of lanes (no clips)
    if (json.isArray()) {
        auto* arr = json.getArray();
        if (arr == nullptr) {
            lastError_ = "Automation data array is invalid";
            return false;
        }

        outLanes.clear();
        outLanes.reserve(arr->size());
        outClips.clear();

        for (const auto& laneVar : *arr) {
            AutomationLaneInfo lane;
            if (!deserializeAutomationLaneInfo(laneVar, lane)) {
                return false;
            }
            outLanes.push_back(std::move(lane));
        }

        return true;
    }

    lastError_ = "Automation data has unexpected format";
    return false;
}

// ============================================================================
// Track serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeTrackInfo(const TrackInfo& track) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", track.id);
    obj->setProperty("type", static_cast<int>(track.type));
    obj->setProperty("name", track.name);
    obj->setProperty("colour", colourToString(track.colour));

    // Hierarchy
    obj->setProperty("parentId", track.parentId);
    juce::Array<juce::var> childIdsArray;
    for (auto childId : track.childIds) {
        childIdsArray.add(childId);
    }
    obj->setProperty("childIds", juce::var(childIdsArray));

    // Mixer state
    obj->setProperty("volume", track.volume);
    obj->setProperty("pan", track.pan);
    obj->setProperty("muted", track.muted);
    obj->setProperty("soloed", track.soloed);
    obj->setProperty("recordArmed", track.recordArmed);
    obj->setProperty("frozen", track.frozen);

    // View settings per view mode
    auto* viewSettingsObj = new juce::DynamicObject();
    for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
        const auto& vs = track.viewSettings.get(mode);
        auto* modeObj = new juce::DynamicObject();
        modeObj->setProperty("visible", vs.visible);
        modeObj->setProperty("locked", vs.locked);
        modeObj->setProperty("collapsed", vs.collapsed);
        modeObj->setProperty("height", vs.height);
        viewSettingsObj->setProperty(getViewModeName(mode), juce::var(modeObj));
    }
    obj->setProperty("viewSettings", juce::var(viewSettingsObj));

    // Routing
    obj->setProperty("midiInputDevice", track.midiInputDevice);
    obj->setProperty("midiOutputDevice", track.midiOutputDevice);
    obj->setProperty("audioInputDevice", track.audioInputDevice);
    obj->setProperty("audioOutputDevice", track.audioOutputDevice);

    // Aux bus index (for aux tracks)
    obj->setProperty("auxBusIndex", track.auxBusIndex);

    // Multi-out link (for MultiOut tracks)
    if (track.multiOutLink.has_value()) {
        auto* moLinkObj = new juce::DynamicObject();
        moLinkObj->setProperty("sourceTrackId", track.multiOutLink->sourceTrackId);
        moLinkObj->setProperty("sourceDeviceId", track.multiOutLink->sourceDeviceId);
        moLinkObj->setProperty("outputPairIndex", track.multiOutLink->outputPairIndex);
        obj->setProperty("multiOutLink", juce::var(moLinkObj));
    }

    // Sends
    juce::Array<juce::var> sendsArray;
    for (const auto& send : track.sends) {
        auto* sendObj = new juce::DynamicObject();
        sendObj->setProperty("busIndex", send.busIndex);
        sendObj->setProperty("level", send.level);
        sendObj->setProperty("preFader", send.preFader);
        sendObj->setProperty("destTrackId", send.destTrackId);
        sendsArray.add(juce::var(sendObj));
    }
    obj->setProperty("sends", juce::var(sendsArray));

    // Chain elements
    juce::Array<juce::var> chainArray;
    for (const auto& element : track.chainElements) {
        chainArray.add(serializeChainElement(element));
    }
    obj->setProperty("chainElements", juce::var(chainArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeTrackInfo(const juce::var& json, TrackInfo& outTrack) {
    if (!json.isObject()) {
        lastError_ = "Track data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outTrack.id = obj->getProperty("id");
    outTrack.type = static_cast<TrackType>(static_cast<int>(obj->getProperty("type")));
    outTrack.name = obj->getProperty("name").toString();
    outTrack.colour = stringToColour(obj->getProperty("colour").toString());

    // Hierarchy
    outTrack.parentId = obj->getProperty("parentId");
    auto childIdsVar = obj->getProperty("childIds");
    if (childIdsVar.isArray()) {
        auto* arr = childIdsVar.getArray();
        for (const auto& idVar : *arr) {
            outTrack.childIds.push_back(static_cast<int>(idVar));
        }
    }

    // Mixer state
    outTrack.volume = obj->getProperty("volume");
    outTrack.pan = obj->getProperty("pan");
    outTrack.muted = obj->getProperty("muted");
    outTrack.soloed = obj->getProperty("soloed");
    outTrack.recordArmed = obj->getProperty("recordArmed");

    // Frozen state (backward compatible - defaults to false)
    if (obj->hasProperty("frozen")) {
        outTrack.frozen = static_cast<bool>(obj->getProperty("frozen"));
    }

    // View settings per view mode (backward compatible - defaults applied if missing)
    auto viewSettingsVar = obj->getProperty("viewSettings");
    if (viewSettingsVar.isObject()) {
        auto* vsObj = viewSettingsVar.getDynamicObject();
        for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
            auto modeVar = vsObj->getProperty(getViewModeName(mode));
            if (modeVar.isObject()) {
                auto* modeObj = modeVar.getDynamicObject();
                TrackViewSettings vs;
                vs.visible = modeObj->getProperty("visible");
                vs.locked = modeObj->getProperty("locked");
                vs.collapsed = modeObj->getProperty("collapsed");
                vs.height = modeObj->getProperty("height");
                outTrack.viewSettings.set(mode, vs);
            }
        }
    }

    // Routing
    outTrack.midiInputDevice = obj->getProperty("midiInputDevice").toString();
    outTrack.midiOutputDevice = obj->getProperty("midiOutputDevice").toString();
    outTrack.audioInputDevice = obj->getProperty("audioInputDevice").toString();
    outTrack.audioOutputDevice = obj->getProperty("audioOutputDevice").toString();

    // Aux bus index (defaults to -1 if not present in older project files)
    if (obj->hasProperty("auxBusIndex")) {
        outTrack.auxBusIndex = obj->getProperty("auxBusIndex");
    }

    // Multi-out link (for MultiOut tracks)
    auto multiOutLinkVar = obj->getProperty("multiOutLink");
    if (multiOutLinkVar.isObject()) {
        auto* moLinkObj = multiOutLinkVar.getDynamicObject();
        MultiOutTrackLink link;
        link.sourceTrackId = moLinkObj->getProperty("sourceTrackId");
        link.sourceDeviceId = moLinkObj->getProperty("sourceDeviceId");
        link.outputPairIndex = moLinkObj->getProperty("outputPairIndex");
        outTrack.multiOutLink = link;
    }

    // Sends
    auto sendsVar = obj->getProperty("sends");
    if (sendsVar.isArray()) {
        auto* arr = sendsVar.getArray();
        for (const auto& sendVar : *arr) {
            if (auto* sendObj = sendVar.getDynamicObject()) {
                SendInfo send;
                send.busIndex = sendObj->getProperty("busIndex");
                send.level = sendObj->getProperty("level");
                send.preFader = sendObj->getProperty("preFader");
                send.destTrackId = sendObj->getProperty("destTrackId");
                outTrack.sends.push_back(send);
            }
        }
    }

    // Chain elements
    auto chainVar = obj->getProperty("chainElements");
    if (chainVar.isArray()) {
        auto* arr = chainVar.getArray();
        for (const auto& elementVar : *arr) {
            ChainElement element;
            if (!deserializeChainElement(elementVar, element)) {
                return false;
            }
            outTrack.chainElements.push_back(std::move(element));
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeChainElement(const ChainElement& element) {
    auto* obj = new juce::DynamicObject();

    if (isDevice(element)) {
        obj->setProperty("type", "device");
        obj->setProperty("device", serializeDeviceInfo(getDevice(element)));
    } else if (isRack(element)) {
        obj->setProperty("type", "rack");
        obj->setProperty("rack", serializeRackInfo(getRack(element)));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainElement(const juce::var& json, ChainElement& outElement) {
    if (!json.isObject()) {
        lastError_ = "Chain element is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();
    juce::String type = obj->getProperty("type").toString();

    if (type == "device") {
        DeviceInfo device;
        if (!deserializeDeviceInfo(obj->getProperty("device"), device)) {
            return false;
        }
        outElement = std::move(device);
    } else if (type == "rack") {
        RackInfo rack;
        if (!deserializeRackInfo(obj->getProperty("rack"), rack)) {
            return false;
        }
        outElement = std::make_unique<RackInfo>(std::move(rack));
    } else {
        lastError_ = "Unknown chain element type: " + type;
        return false;
    }

    return true;
}

juce::var ProjectSerializer::serializeDeviceInfo(const DeviceInfo& device) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", device.id);
    obj->setProperty("name", device.name);
    obj->setProperty("pluginId", device.pluginId);
    obj->setProperty("manufacturer", device.manufacturer);
    obj->setProperty("format", static_cast<int>(device.format));
    obj->setProperty("isInstrument", device.isInstrument);
    obj->setProperty("uniqueId", device.uniqueId);
    obj->setProperty("fileOrIdentifier", device.fileOrIdentifier);
    obj->setProperty("bypassed", device.bypassed);
    obj->setProperty("expanded", device.expanded);
    obj->setProperty("modPanelOpen", device.modPanelOpen);
    obj->setProperty("gainPanelOpen", device.gainPanelOpen);
    obj->setProperty("paramPanelOpen", device.paramPanelOpen);

    // Parameters
    juce::Array<juce::var> paramsArray;
    for (const auto& param : device.parameters) {
        paramsArray.add(serializeParameterInfo(param));
    }
    obj->setProperty("parameters", juce::var(paramsArray));

    // Visible parameters
    juce::Array<juce::var> visibleParamsArray;
    for (auto index : device.visibleParameters) {
        visibleParamsArray.add(index);
    }
    obj->setProperty("visibleParameters", juce::var(visibleParamsArray));

    // Gain stage
    obj->setProperty("gainParameterIndex", device.gainParameterIndex);
    obj->setProperty("gainValue", device.gainValue);
    obj->setProperty("gainDb", device.gainDb);

    // Macros
    juce::Array<juce::var> macrosArray;
    for (const auto& macro : device.macros) {
        macrosArray.add(serializeMacroInfo(macro));
    }
    obj->setProperty("macros", juce::var(macrosArray));

    // Mods
    juce::Array<juce::var> modsArray;
    for (const auto& mod : device.mods) {
        modsArray.add(serializeModInfo(mod));
    }
    obj->setProperty("mods", juce::var(modsArray));

    obj->setProperty("currentParameterPage", device.currentParameterPage);

    // Multi-output config
    if (device.multiOut.isMultiOut) {
        auto* multiOutObj = new juce::DynamicObject();
        multiOutObj->setProperty("isMultiOut", true);
        multiOutObj->setProperty("totalOutputChannels", device.multiOut.totalOutputChannels);
        multiOutObj->setProperty("mixerChildrenCollapsed", device.multiOut.mixerChildrenCollapsed);

        juce::Array<juce::var> pairsArray;
        for (const auto& pair : device.multiOut.outputPairs) {
            auto* pairObj = new juce::DynamicObject();
            pairObj->setProperty("outputIndex", pair.outputIndex);
            pairObj->setProperty("name", pair.name);
            pairObj->setProperty("active", pair.active);
            pairObj->setProperty("trackId", pair.trackId);
            pairObj->setProperty("firstPin", pair.firstPin);
            pairObj->setProperty("numChannels", pair.numChannels);
            pairsArray.add(juce::var(pairObj));
        }
        multiOutObj->setProperty("outputPairs", juce::var(pairsArray));
        obj->setProperty("multiOut", juce::var(multiOutObj));
    }

    // Sidechain / MIDI receive capabilities
    if (device.canSidechain) {
        obj->setProperty("canSidechain", true);
    }
    if (device.canReceiveMidi) {
        obj->setProperty("canReceiveMidi", true);
    }

    // Sidechain
    if (device.sidechain.isActive()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(device.sidechain.type));
        scObj->setProperty("sourceTrackId", device.sidechain.sourceTrackId);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeDeviceInfo(const juce::var& json, DeviceInfo& outDevice) {
    if (!json.isObject()) {
        lastError_ = "Device data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outDevice.id = obj->getProperty("id");
    outDevice.name = obj->getProperty("name").toString();
    outDevice.pluginId = obj->getProperty("pluginId").toString();
    outDevice.manufacturer = obj->getProperty("manufacturer").toString();
    outDevice.format = static_cast<PluginFormat>(static_cast<int>(obj->getProperty("format")));
    outDevice.isInstrument = obj->getProperty("isInstrument");
    outDevice.uniqueId = obj->getProperty("uniqueId").toString();
    outDevice.fileOrIdentifier = obj->getProperty("fileOrIdentifier").toString();
    outDevice.bypassed = obj->getProperty("bypassed");
    outDevice.expanded = obj->getProperty("expanded");
    outDevice.modPanelOpen = obj->getProperty("modPanelOpen");
    outDevice.gainPanelOpen = obj->getProperty("gainPanelOpen");
    outDevice.paramPanelOpen = obj->getProperty("paramPanelOpen");

    // Parameters
    auto paramsVar = obj->getProperty("parameters");
    if (paramsVar.isArray()) {
        auto* arr = paramsVar.getArray();
        for (const auto& paramVar : *arr) {
            ParameterInfo param;
            if (!deserializeParameterInfo(paramVar, param)) {
                return false;
            }
            outDevice.parameters.push_back(param);
        }
    }

    // Visible parameters
    auto visibleParamsVar = obj->getProperty("visibleParameters");
    if (visibleParamsVar.isArray()) {
        auto* arr = visibleParamsVar.getArray();
        for (const auto& indexVar : *arr) {
            outDevice.visibleParameters.push_back(static_cast<int>(indexVar));
        }
    }

    // Gain stage
    outDevice.gainParameterIndex = obj->getProperty("gainParameterIndex");
    outDevice.gainValue = obj->getProperty("gainValue");
    outDevice.gainDb = obj->getProperty("gainDb");

    // Macros
    auto macrosVar = obj->getProperty("macros");
    if (macrosVar.isArray()) {
        auto* arr = macrosVar.getArray();
        outDevice.macros.clear();
        for (const auto& macroVar : *arr) {
            MacroInfo macro;
            if (!deserializeMacroInfo(macroVar, macro)) {
                return false;
            }
            outDevice.macros.push_back(macro);
        }
    }

    // Mods
    auto modsVar = obj->getProperty("mods");
    if (modsVar.isArray()) {
        auto* arr = modsVar.getArray();
        outDevice.mods.clear();
        for (const auto& modVar : *arr) {
            ModInfo mod;
            if (!deserializeModInfo(modVar, mod)) {
                return false;
            }
            outDevice.mods.push_back(mod);
        }
    }

    outDevice.currentParameterPage = obj->getProperty("currentParameterPage");

    // Multi-output config
    auto multiOutVar = obj->getProperty("multiOut");
    if (multiOutVar.isObject()) {
        auto* moObj = multiOutVar.getDynamicObject();
        outDevice.multiOut.isMultiOut = moObj->getProperty("isMultiOut");
        outDevice.multiOut.totalOutputChannels = moObj->getProperty("totalOutputChannels");
        if (moObj->hasProperty("mixerChildrenCollapsed"))
            outDevice.multiOut.mixerChildrenCollapsed =
                moObj->getProperty("mixerChildrenCollapsed");

        auto pairsVar = moObj->getProperty("outputPairs");
        if (pairsVar.isArray()) {
            auto* pairsArr = pairsVar.getArray();
            for (const auto& pairVar : *pairsArr) {
                if (auto* pairObj = pairVar.getDynamicObject()) {
                    MultiOutOutputPair pair;
                    pair.outputIndex = pairObj->getProperty("outputIndex");
                    pair.name = pairObj->getProperty("name").toString();
                    pair.active = pairObj->getProperty("active");
                    pair.trackId = pairObj->getProperty("trackId");
                    if (pairObj->hasProperty("firstPin"))
                        pair.firstPin = pairObj->getProperty("firstPin");
                    if (pairObj->hasProperty("numChannels"))
                        pair.numChannels = pairObj->getProperty("numChannels");
                    outDevice.multiOut.outputPairs.push_back(pair);
                }
            }
        }
    }

    // Sidechain / MIDI receive capabilities
    auto canSidechainVar = obj->getProperty("canSidechain");
    if (!canSidechainVar.isVoid()) {
        outDevice.canSidechain = static_cast<bool>(canSidechainVar);
    }
    auto canReceiveMidiVar = obj->getProperty("canReceiveMidi");
    if (!canReceiveMidiVar.isVoid()) {
        outDevice.canReceiveMidi = static_cast<bool>(canReceiveMidiVar);
    }

    // Sidechain
    auto sidechainVar = obj->getProperty("sidechain");
    if (sidechainVar.isObject()) {
        auto* scObj = sidechainVar.getDynamicObject();
        outDevice.sidechain.type =
            static_cast<SidechainConfig::Type>(static_cast<int>(scObj->getProperty("type")));
        outDevice.sidechain.sourceTrackId = scObj->getProperty("sourceTrackId");
    }

    return true;
}

juce::var ProjectSerializer::serializeRackInfo(const RackInfo& rack) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", rack.id);
    obj->setProperty("name", rack.name);
    obj->setProperty("bypassed", rack.bypassed);
    obj->setProperty("expanded", rack.expanded);
    obj->setProperty("volume", rack.volume);
    obj->setProperty("pan", rack.pan);

    // Chains
    juce::Array<juce::var> chainsArray;
    for (const auto& chain : rack.chains) {
        chainsArray.add(serializeChainInfo(chain));
    }
    obj->setProperty("chains", juce::var(chainsArray));

    // Macros
    juce::Array<juce::var> macrosArray;
    for (const auto& macro : rack.macros) {
        macrosArray.add(serializeMacroInfo(macro));
    }
    obj->setProperty("macros", juce::var(macrosArray));

    // Mods
    juce::Array<juce::var> modsArray;
    for (const auto& mod : rack.mods) {
        modsArray.add(serializeModInfo(mod));
    }
    obj->setProperty("mods", juce::var(modsArray));

    // Sidechain
    if (rack.sidechain.isActive()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(rack.sidechain.type));
        scObj->setProperty("sourceTrackId", rack.sidechain.sourceTrackId);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeRackInfo(const juce::var& json, RackInfo& outRack) {
    if (!json.isObject()) {
        lastError_ = "Rack data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outRack.id = obj->getProperty("id");
    outRack.name = obj->getProperty("name").toString();
    outRack.bypassed = obj->getProperty("bypassed");
    outRack.expanded = obj->getProperty("expanded");
    outRack.volume = obj->getProperty("volume");
    outRack.pan = obj->getProperty("pan");

    // Chains
    auto chainsVar = obj->getProperty("chains");
    if (chainsVar.isArray()) {
        auto* arr = chainsVar.getArray();
        outRack.chains.clear();
        for (const auto& chainVar : *arr) {
            ChainInfo chain;
            if (!deserializeChainInfo(chainVar, chain)) {
                return false;
            }
            outRack.chains.push_back(std::move(chain));
        }
    }

    // Macros
    auto macrosVar = obj->getProperty("macros");
    if (macrosVar.isArray()) {
        auto* arr = macrosVar.getArray();
        outRack.macros.clear();
        for (const auto& macroVar : *arr) {
            MacroInfo macro;
            if (!deserializeMacroInfo(macroVar, macro)) {
                return false;
            }
            outRack.macros.push_back(macro);
        }
    }

    // Mods
    auto modsVar = obj->getProperty("mods");
    if (modsVar.isArray()) {
        auto* arr = modsVar.getArray();
        outRack.mods.clear();
        for (const auto& modVar : *arr) {
            ModInfo mod;
            if (!deserializeModInfo(modVar, mod)) {
                return false;
            }
            outRack.mods.push_back(mod);
        }
    }

    // Sidechain
    auto sidechainVar = obj->getProperty("sidechain");
    if (sidechainVar.isObject()) {
        auto* scObj = sidechainVar.getDynamicObject();
        outRack.sidechain.type =
            static_cast<SidechainConfig::Type>(static_cast<int>(scObj->getProperty("type")));
        outRack.sidechain.sourceTrackId = scObj->getProperty("sourceTrackId");
    }

    return true;
}

juce::var ProjectSerializer::serializeChainInfo(const ChainInfo& chain) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", chain.id);
    obj->setProperty("name", chain.name);
    obj->setProperty("outputIndex", chain.outputIndex);
    obj->setProperty("muted", chain.muted);
    obj->setProperty("solo", chain.solo);
    obj->setProperty("volume", chain.volume);
    obj->setProperty("pan", chain.pan);
    obj->setProperty("expanded", chain.expanded);

    // Elements
    juce::Array<juce::var> elementsArray;
    for (const auto& element : chain.elements) {
        elementsArray.add(serializeChainElement(element));
    }
    obj->setProperty("elements", juce::var(elementsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainInfo(const juce::var& json, ChainInfo& outChain) {
    if (!json.isObject()) {
        lastError_ = "Chain data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outChain.id = obj->getProperty("id");
    outChain.name = obj->getProperty("name").toString();
    outChain.outputIndex = obj->getProperty("outputIndex");
    outChain.muted = obj->getProperty("muted");
    outChain.solo = obj->getProperty("solo");
    outChain.volume = obj->getProperty("volume");
    outChain.pan = obj->getProperty("pan");
    outChain.expanded = obj->getProperty("expanded");

    // Elements
    auto elementsVar = obj->getProperty("elements");
    if (elementsVar.isArray()) {
        auto* arr = elementsVar.getArray();
        outChain.elements.clear();
        for (const auto& elementVar : *arr) {
            ChainElement element;
            if (!deserializeChainElement(elementVar, element)) {
                return false;
            }
            outChain.elements.push_back(std::move(element));
        }
    }

    return true;
}

// ============================================================================
// Clip serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeClipInfo(const ClipInfo& clip) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", clip.id);
    obj->setProperty("trackId", clip.trackId);
    obj->setProperty("name", clip.name);
    obj->setProperty("colour", colourToString(clip.colour));
    obj->setProperty("type", static_cast<int>(clip.type));
    obj->setProperty("startTime", clip.startTime);
    obj->setProperty("length", clip.length);
    obj->setProperty("view", static_cast<int>(clip.view));
    obj->setProperty("loopEnabled", clip.loopEnabled);
    obj->setProperty("loopStart", clip.loopStart);
    obj->setProperty("loopLength", clip.loopLength);
    obj->setProperty("sceneIndex", clip.sceneIndex);
    obj->setProperty("launchMode", static_cast<int>(clip.launchMode));
    obj->setProperty("launchQuantize", static_cast<int>(clip.launchQuantize));

    // Per-clip grid settings
    obj->setProperty("gridAutoGrid", clip.gridAutoGrid);
    obj->setProperty("gridNumerator", clip.gridNumerator);
    obj->setProperty("gridDenominator", clip.gridDenominator);
    obj->setProperty("gridSnapEnabled", clip.gridSnapEnabled);

    // Per-clip mix
    obj->setProperty("volumeDB", clip.volumeDB);
    obj->setProperty("gainDB", clip.gainDB);
    obj->setProperty("pan", clip.pan);

    // Fades
    obj->setProperty("fadeIn", clip.fadeIn);
    obj->setProperty("fadeOut", clip.fadeOut);
    obj->setProperty("fadeInType", clip.fadeInType);
    obj->setProperty("fadeOutType", clip.fadeOutType);
    obj->setProperty("fadeInBehaviour", clip.fadeInBehaviour);
    obj->setProperty("fadeOutBehaviour", clip.fadeOutBehaviour);
    obj->setProperty("autoCrossfade", clip.autoCrossfade);

    // Pitch
    obj->setProperty("pitchChange", clip.pitchChange);
    obj->setProperty("transpose", clip.transpose);
    obj->setProperty("autoPitch", clip.autoPitch);
    obj->setProperty("autoPitchMode", clip.autoPitchMode);

    // Playback
    obj->setProperty("isReversed", clip.isReversed);

    // Beat detection
    obj->setProperty("autoDetectBeats", clip.autoDetectBeats);
    obj->setProperty("beatSensitivity", clip.beatSensitivity);

    // Channels
    obj->setProperty("leftChannelActive", clip.leftChannelActive);
    obj->setProperty("rightChannelActive", clip.rightChannelActive);

    // Auto-tempo / Musical mode & beat-based properties
    obj->setProperty("autoTempo", clip.autoTempo);
    obj->setProperty("startBeats", clip.startBeats);
    obj->setProperty("lengthBeats", clip.lengthBeats);
    obj->setProperty("loopStartBeats", clip.loopStartBeats);
    obj->setProperty("loopLengthBeats", clip.loopLengthBeats);

    // Source metadata
    if (clip.sourceNumBeats > 0.0)
        obj->setProperty("sourceNumBeats", clip.sourceNumBeats);
    if (clip.sourceBPM > 0.0)
        obj->setProperty("sourceBPM", clip.sourceBPM);

    // MIDI offset
    if (clip.midiOffset != 0.0)
        obj->setProperty("midiOffset", clip.midiOffset);

    // Audio properties (TE-aligned model)
    if (clip.audioFilePath.isNotEmpty()) {
        obj->setProperty("audioFilePath", clip.audioFilePath);
        obj->setProperty("offset", clip.offset);
        obj->setProperty("speedRatio", clip.speedRatio);
        if (clip.warpEnabled) {
            obj->setProperty("warpEnabled", clip.warpEnabled);
        }
        if (clip.analogPitch) {
            obj->setProperty("analogPitch", clip.analogPitch);
        }
        if (clip.timeStretchMode != 0) {
            obj->setProperty("timeStretchMode", clip.timeStretchMode);
        }
    }

    // MIDI notes
    juce::Array<juce::var> midiNotesArray;
    for (const auto& note : clip.midiNotes) {
        midiNotesArray.add(serializeMidiNote(note));
    }
    obj->setProperty("midiNotes", juce::var(midiNotesArray));

    // MIDI CC data
    if (!clip.midiCCData.empty()) {
        juce::Array<juce::var> ccArray;
        for (const auto& cc : clip.midiCCData) {
            auto* ccObj = new juce::DynamicObject();
            ccObj->setProperty("controller", cc.controller);
            ccObj->setProperty("value", cc.value);
            ccObj->setProperty("beatPosition", cc.beatPosition);
            ccArray.add(juce::var(ccObj));
        }
        obj->setProperty("midiCCData", juce::var(ccArray));
    }

    // MIDI pitch bend data
    if (!clip.midiPitchBendData.empty()) {
        juce::Array<juce::var> pbArray;
        for (const auto& pb : clip.midiPitchBendData) {
            auto* pbObj = new juce::DynamicObject();
            pbObj->setProperty("value", pb.value);
            pbObj->setProperty("beatPosition", pb.beatPosition);
            pbArray.add(juce::var(pbObj));
        }
        obj->setProperty("midiPitchBendData", juce::var(pbArray));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeClipInfo(const juce::var& json, ClipInfo& outClip,
                                            double projectTempo) {
    if (!json.isObject()) {
        lastError_ = "Clip data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outClip.id = obj->getProperty("id");
    outClip.trackId = obj->getProperty("trackId");
    outClip.name = obj->getProperty("name").toString();
    outClip.colour = stringToColour(obj->getProperty("colour").toString());
    outClip.type = static_cast<ClipType>(static_cast<int>(obj->getProperty("type")));
    outClip.startTime = obj->getProperty("startTime");
    outClip.length = obj->getProperty("length");
    // View type (backward compatible - defaults to Arrangement if missing)
    auto viewVar = obj->getProperty("view");
    if (!viewVar.isVoid()) {
        outClip.view = static_cast<ClipView>(static_cast<int>(viewVar));
    }
    // Loop settings
    outClip.loopEnabled = static_cast<bool>(obj->getProperty("loopEnabled"));
    outClip.loopStart = obj->getProperty("loopStart");
    outClip.loopLength = obj->getProperty("loopLength");
    outClip.sceneIndex = obj->getProperty("sceneIndex");

    // Launch properties
    outClip.launchMode = static_cast<LaunchMode>(static_cast<int>(obj->getProperty("launchMode")));
    outClip.launchQuantize =
        static_cast<LaunchQuantize>(static_cast<int>(obj->getProperty("launchQuantize")));

    // Per-clip grid settings
    outClip.gridAutoGrid = static_cast<bool>(obj->getProperty("gridAutoGrid"));
    outClip.gridNumerator = obj->getProperty("gridNumerator");
    outClip.gridDenominator = obj->getProperty("gridDenominator");
    outClip.gridSnapEnabled = static_cast<bool>(obj->getProperty("gridSnapEnabled"));

    // Per-clip mix
    outClip.volumeDB = static_cast<float>(static_cast<double>(obj->getProperty("volumeDB")));
    outClip.gainDB = static_cast<float>(static_cast<double>(obj->getProperty("gainDB")));
    outClip.pan = static_cast<float>(static_cast<double>(obj->getProperty("pan")));

    // Fades
    outClip.fadeIn = obj->getProperty("fadeIn");
    outClip.fadeOut = obj->getProperty("fadeOut");
    outClip.fadeInType = obj->getProperty("fadeInType");
    outClip.fadeOutType = obj->getProperty("fadeOutType");
    outClip.fadeInBehaviour = obj->getProperty("fadeInBehaviour");
    outClip.fadeOutBehaviour = obj->getProperty("fadeOutBehaviour");
    outClip.autoCrossfade = static_cast<bool>(obj->getProperty("autoCrossfade"));

    // Pitch
    outClip.pitchChange = static_cast<float>(static_cast<double>(obj->getProperty("pitchChange")));
    outClip.transpose = obj->getProperty("transpose");
    outClip.autoPitch = static_cast<bool>(obj->getProperty("autoPitch"));
    outClip.autoPitchMode = obj->getProperty("autoPitchMode");

    // Playback
    outClip.isReversed = static_cast<bool>(obj->getProperty("isReversed"));

    // Beat detection
    outClip.autoDetectBeats = static_cast<bool>(obj->getProperty("autoDetectBeats"));
    outClip.beatSensitivity =
        static_cast<float>(static_cast<double>(obj->getProperty("beatSensitivity")));

    // Channels
    outClip.leftChannelActive = static_cast<bool>(obj->getProperty("leftChannelActive"));
    outClip.rightChannelActive = static_cast<bool>(obj->getProperty("rightChannelActive"));

    // Auto-tempo / Musical mode & beat-based properties
    outClip.autoTempo = static_cast<bool>(obj->getProperty("autoTempo"));
    outClip.startBeats = obj->getProperty("startBeats");
    outClip.lengthBeats = obj->getProperty("lengthBeats");
    outClip.loopStartBeats = obj->getProperty("loopStartBeats");
    outClip.loopLengthBeats = obj->getProperty("loopLengthBeats");

    // Source metadata
    outClip.sourceNumBeats = obj->getProperty("sourceNumBeats");
    outClip.sourceBPM = obj->getProperty("sourceBPM");

    // MIDI offset
    outClip.midiOffset = obj->getProperty("midiOffset");

    // Audio properties
    auto audioFilePathVar = obj->getProperty("audioFilePath");
    if (!audioFilePathVar.isVoid()) {
        outClip.audioFilePath = audioFilePathVar.toString();
        outClip.offset = obj->getProperty("offset");
        outClip.speedRatio = obj->getProperty("speedRatio");
        if (outClip.speedRatio <= 0.0)
            outClip.speedRatio = 1.0;
        outClip.warpEnabled = static_cast<bool>(obj->getProperty("warpEnabled"));
        outClip.analogPitch = static_cast<bool>(obj->getProperty("analogPitch"));
        outClip.timeStretchMode = obj->getProperty("timeStretchMode");
    }

    // MIDI notes
    auto midiNotesVar = obj->getProperty("midiNotes");
    if (midiNotesVar.isArray()) {
        auto* arr = midiNotesVar.getArray();
        for (const auto& noteVar : *arr) {
            MidiNote note;
            if (!deserializeMidiNote(noteVar, note)) {
                return false;
            }
            outClip.midiNotes.push_back(note);
        }
    }

    // MIDI CC data
    auto midiCCVar = obj->getProperty("midiCCData");
    if (midiCCVar.isArray()) {
        auto* arr = midiCCVar.getArray();
        for (const auto& ccVar : *arr) {
            if (ccVar.isObject()) {
                auto* ccObj = ccVar.getDynamicObject();
                MidiCCData cc;
                cc.controller = ccObj->getProperty("controller");
                cc.value = ccObj->getProperty("value");
                cc.beatPosition = ccObj->getProperty("beatPosition");
                outClip.midiCCData.push_back(cc);
            }
        }
    }

    // MIDI pitch bend data
    auto midiPBVar = obj->getProperty("midiPitchBendData");
    if (midiPBVar.isArray()) {
        auto* arr = midiPBVar.getArray();
        for (const auto& pbVar : *arr) {
            if (pbVar.isObject()) {
                auto* pbObj = pbVar.getDynamicObject();
                MidiPitchBendData pb;
                pb.value = pbObj->getProperty("value");
                pb.beatPosition = pbObj->getProperty("beatPosition");
                outClip.midiPitchBendData.push_back(pb);
            }
        }
    }

    // MIDI clips: ensure beats are populated (backward compat with old project files)
    if (outClip.type == ClipType::MIDI) {
        if (outClip.lengthBeats <= 0.0 && projectTempo > 0.0) {
            outClip.startBeats = (outClip.startTime * projectTempo) / 60.0;
            outClip.lengthBeats = (outClip.length * projectTempo) / 60.0;
        }
        // Derive seconds cache from authoritative beats
        outClip.deriveTimesFromBeats(projectTempo);
    }

    return true;
}

juce::var ProjectSerializer::serializeMidiNote(const MidiNote& note) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("noteNumber", note.noteNumber);
    obj->setProperty("velocity", note.velocity);
    obj->setProperty("startBeat", note.startBeat);
    obj->setProperty("lengthBeats", note.lengthBeats);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeMidiNote(const juce::var& json, MidiNote& outNote) {
    if (!json.isObject()) {
        lastError_ = "MIDI note is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outNote.noteNumber = obj->getProperty("noteNumber");
    outNote.velocity = obj->getProperty("velocity");
    outNote.startBeat = obj->getProperty("startBeat");
    outNote.lengthBeats = obj->getProperty("lengthBeats");

    return true;
}

// ============================================================================
// Automation serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeAutomationLaneInfo(const AutomationLaneInfo& lane) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", lane.id);
    obj->setProperty("target", serializeAutomationTarget(lane.target));
    obj->setProperty("type", static_cast<int>(lane.type));
    obj->setProperty("name", lane.name);
    obj->setProperty("visible", lane.visible);
    obj->setProperty("expanded", lane.expanded);
    obj->setProperty("armed", lane.armed);
    obj->setProperty("height", lane.height);

    // Absolute points
    juce::Array<juce::var> pointsArray;
    for (const auto& point : lane.absolutePoints) {
        pointsArray.add(serializeAutomationPoint(point));
    }
    obj->setProperty("absolutePoints", juce::var(pointsArray));

    // Clip IDs
    juce::Array<juce::var> clipIdsArray;
    for (auto clipId : lane.clipIds) {
        clipIdsArray.add(clipId);
    }
    obj->setProperty("clipIds", juce::var(clipIdsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationLaneInfo(const juce::var& json,
                                                      AutomationLaneInfo& outLane) {
    if (!json.isObject()) {
        lastError_ = "Automation lane is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outLane.id = obj->getProperty("id");
    if (!deserializeAutomationTarget(obj->getProperty("target"), outLane.target)) {
        return false;
    }
    outLane.type = static_cast<AutomationLaneType>(static_cast<int>(obj->getProperty("type")));
    outLane.name = obj->getProperty("name").toString();
    outLane.visible = obj->getProperty("visible");
    outLane.expanded = obj->getProperty("expanded");
    outLane.armed = obj->getProperty("armed");
    outLane.height = obj->getProperty("height");

    // Absolute points
    auto pointsVar = obj->getProperty("absolutePoints");
    if (pointsVar.isArray()) {
        auto* arr = pointsVar.getArray();
        for (const auto& pointVar : *arr) {
            AutomationPoint point;
            if (!deserializeAutomationPoint(pointVar, point)) {
                return false;
            }
            outLane.absolutePoints.push_back(point);
        }
    }

    // Clip IDs
    auto clipIdsVar = obj->getProperty("clipIds");
    if (clipIdsVar.isArray()) {
        auto* arr = clipIdsVar.getArray();
        for (const auto& idVar : *arr) {
            outLane.clipIds.push_back(static_cast<int>(idVar));
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationClipInfo(const AutomationClipInfo& clip) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", clip.id);
    obj->setProperty("laneId", clip.laneId);
    obj->setProperty("name", clip.name);
    obj->setProperty("colour", colourToString(clip.colour));
    obj->setProperty("startTime", clip.startTime);
    obj->setProperty("length", clip.length);
    obj->setProperty("looping", clip.looping);
    obj->setProperty("loopLength", clip.loopLength);

    // Points
    juce::Array<juce::var> pointsArray;
    for (const auto& point : clip.points) {
        pointsArray.add(serializeAutomationPoint(point));
    }
    obj->setProperty("points", juce::var(pointsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationClipInfo(const juce::var& json,
                                                      AutomationClipInfo& outClip) {
    if (!json.isObject()) {
        lastError_ = "Automation clip is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outClip.id = obj->getProperty("id");
    outClip.laneId = obj->getProperty("laneId");
    outClip.name = obj->getProperty("name").toString();
    outClip.colour = stringToColour(obj->getProperty("colour").toString());
    outClip.startTime = obj->getProperty("startTime");
    outClip.length = obj->getProperty("length");
    outClip.looping = obj->getProperty("looping");
    outClip.loopLength = obj->getProperty("loopLength");

    // Points
    auto pointsVar = obj->getProperty("points");
    if (pointsVar.isArray()) {
        auto* arr = pointsVar.getArray();
        for (const auto& pointVar : *arr) {
            AutomationPoint point;
            if (!deserializeAutomationPoint(pointVar, point)) {
                return false;
            }
            outClip.points.push_back(point);
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationPoint(const AutomationPoint& point) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", point.id);
    obj->setProperty("time", point.time);
    obj->setProperty("value", point.value);
    obj->setProperty("curveType", static_cast<int>(point.curveType));
    obj->setProperty("tension", point.tension);
    obj->setProperty("inHandle", serializeBezierHandle(point.inHandle));
    obj->setProperty("outHandle", serializeBezierHandle(point.outHandle));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationPoint(const juce::var& json,
                                                   AutomationPoint& outPoint) {
    if (!json.isObject()) {
        lastError_ = "Automation point is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outPoint.id = obj->getProperty("id");
    outPoint.time = obj->getProperty("time");
    outPoint.value = obj->getProperty("value");
    outPoint.curveType =
        static_cast<AutomationCurveType>(static_cast<int>(obj->getProperty("curveType")));
    outPoint.tension = obj->getProperty("tension");

    if (!deserializeBezierHandle(obj->getProperty("inHandle"), outPoint.inHandle)) {
        return false;
    }
    if (!deserializeBezierHandle(obj->getProperty("outHandle"), outPoint.outHandle)) {
        return false;
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationTarget(const AutomationTarget& target) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("type", static_cast<int>(target.type));
    obj->setProperty("trackId", target.trackId);
    obj->setProperty("devicePath", serializeChainNodePath(target.devicePath));
    obj->setProperty("paramIndex", target.paramIndex);
    obj->setProperty("macroIndex", target.macroIndex);
    obj->setProperty("modId", target.modId);
    obj->setProperty("modParamIndex", target.modParamIndex);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationTarget(const juce::var& json,
                                                    AutomationTarget& outTarget) {
    if (!json.isObject()) {
        lastError_ = "Automation target is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outTarget.type = static_cast<AutomationTargetType>(static_cast<int>(obj->getProperty("type")));
    outTarget.trackId = obj->getProperty("trackId");
    if (!deserializeChainNodePath(obj->getProperty("devicePath"), outTarget.devicePath)) {
        return false;
    }
    outTarget.paramIndex = obj->getProperty("paramIndex");
    outTarget.macroIndex = obj->getProperty("macroIndex");
    outTarget.modId = obj->getProperty("modId");
    outTarget.modParamIndex = obj->getProperty("modParamIndex");

    return true;
}

juce::var ProjectSerializer::serializeBezierHandle(const BezierHandle& handle) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("time", handle.time);
    obj->setProperty("value", handle.value);
    obj->setProperty("linked", handle.linked);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeBezierHandle(const juce::var& json, BezierHandle& outHandle) {
    if (!json.isObject()) {
        lastError_ = "Bezier handle is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outHandle.time = obj->getProperty("time");
    outHandle.value = obj->getProperty("value");
    outHandle.linked = obj->getProperty("linked");

    return true;
}

juce::var ProjectSerializer::serializeChainNodePath(const ChainNodePath& path) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("trackId", path.trackId);
    obj->setProperty("topLevelDeviceId", path.topLevelDeviceId);

    juce::Array<juce::var> stepsArray;
    for (const auto& step : path.steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", static_cast<int>(step.type));
        stepObj->setProperty("id", step.id);
        stepsArray.add(juce::var(stepObj));
    }
    obj->setProperty("steps", juce::var(stepsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainNodePath(const juce::var& json, ChainNodePath& outPath) {
    if (!json.isObject()) {
        lastError_ = "Chain node path is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outPath.trackId = obj->getProperty("trackId");
    outPath.topLevelDeviceId = obj->getProperty("topLevelDeviceId");

    auto stepsVar = obj->getProperty("steps");
    if (stepsVar.isArray()) {
        auto* arr = stepsVar.getArray();
        for (const auto& stepVar : *arr) {
            if (!stepVar.isObject())
                continue;
            auto* stepObj = stepVar.getDynamicObject();
            ChainPathStep step;
            step.type = static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
            step.id = stepObj->getProperty("id");
            outPath.steps.push_back(step);
        }
    }

    return true;
}

// ============================================================================
// Macro and Mod serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeMacroInfo(const MacroInfo& macro) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", macro.id);
    obj->setProperty("name", macro.name);
    obj->setProperty("value", macro.value);

    // Legacy target
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", macro.target.deviceId);
    targetObj->setProperty("paramIndex", macro.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));

    // Links
    juce::Array<juce::var> linksArray;
    for (const auto& link : macro.links) {
        auto* linkObj = new juce::DynamicObject();
        auto* linkTargetObj = new juce::DynamicObject();
        linkTargetObj->setProperty("deviceId", link.target.deviceId);
        linkTargetObj->setProperty("paramIndex", link.target.paramIndex);
        linkObj->setProperty("target", juce::var(linkTargetObj));
        linkObj->setProperty("amount", link.amount);
        linkObj->setProperty("bipolar", link.bipolar);
        linksArray.add(juce::var(linkObj));
    }
    obj->setProperty("links", juce::var(linksArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeMacroInfo(const juce::var& json, MacroInfo& outMacro) {
    if (!json.isObject()) {
        lastError_ = "Macro is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outMacro.id = obj->getProperty("id");
    outMacro.name = obj->getProperty("name").toString();
    outMacro.value = obj->getProperty("value");

    // Legacy target
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        outMacro.target.deviceId = targetObj->getProperty("deviceId");
        outMacro.target.paramIndex = targetObj->getProperty("paramIndex");
    }

    // Links
    auto linksVar = obj->getProperty("links");
    if (linksVar.isArray()) {
        auto* arr = linksVar.getArray();
        for (const auto& linkVar : *arr) {
            if (!linkVar.isObject())
                continue;
            auto* linkObj = linkVar.getDynamicObject();
            MacroLink link;
            auto targetVar2 = linkObj->getProperty("target");
            if (targetVar2.isObject()) {
                auto* targetObj = targetVar2.getDynamicObject();
                link.target.deviceId = targetObj->getProperty("deviceId");
                link.target.paramIndex = targetObj->getProperty("paramIndex");
            }
            link.amount = linkObj->getProperty("amount");
            if (linkObj->hasProperty("bipolar"))
                link.bipolar = static_cast<bool>(linkObj->getProperty("bipolar"));
            outMacro.links.push_back(link);
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeModInfo(const ModInfo& mod) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", mod.id);
    obj->setProperty("name", mod.name);
    obj->setProperty("type", static_cast<int>(mod.type));
    obj->setProperty("enabled", mod.enabled);
    obj->setProperty("rate", mod.rate);
    obj->setProperty("waveform", static_cast<int>(mod.waveform));
    obj->setProperty("phase", mod.phase);
    obj->setProperty("phaseOffset", mod.phaseOffset);
    obj->setProperty("value", mod.value);
    obj->setProperty("tempoSync", mod.tempoSync);
    obj->setProperty("syncDivision", static_cast<int>(mod.syncDivision));
    obj->setProperty("triggerMode", static_cast<int>(mod.triggerMode));
    obj->setProperty("oneShot", mod.oneShot);
    obj->setProperty("useLoopRegion", mod.useLoopRegion);
    obj->setProperty("loopStart", mod.loopStart);
    obj->setProperty("loopEnd", mod.loopEnd);
    obj->setProperty("midiChannel", mod.midiChannel);
    obj->setProperty("midiNote", mod.midiNote);
    obj->setProperty("audioAttackMs", mod.audioAttackMs);
    obj->setProperty("audioReleaseMs", mod.audioReleaseMs);
    obj->setProperty("curvePreset", static_cast<int>(mod.curvePreset));

    // Curve points
    juce::Array<juce::var> curvePointsArray;
    for (const auto& point : mod.curvePoints) {
        auto* pointObj = new juce::DynamicObject();
        pointObj->setProperty("phase", point.phase);
        pointObj->setProperty("value", point.value);
        pointObj->setProperty("tension", point.tension);
        curvePointsArray.add(juce::var(pointObj));
    }
    obj->setProperty("curvePoints", juce::var(curvePointsArray));

    // Links
    juce::Array<juce::var> linksArray;
    for (const auto& link : mod.links) {
        auto* linkObj = new juce::DynamicObject();
        auto* linkTargetObj = new juce::DynamicObject();
        linkTargetObj->setProperty("deviceId", link.target.deviceId);
        linkTargetObj->setProperty("paramIndex", link.target.paramIndex);
        linkObj->setProperty("target", juce::var(linkTargetObj));
        linkObj->setProperty("amount", link.amount);
        linkObj->setProperty("bipolar", link.bipolar);
        linksArray.add(juce::var(linkObj));
    }
    obj->setProperty("links", juce::var(linksArray));

    // Legacy target/amount
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", mod.target.deviceId);
    targetObj->setProperty("paramIndex", mod.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));
    obj->setProperty("amount", mod.amount);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeModInfo(const juce::var& json, ModInfo& outMod) {
    if (!json.isObject()) {
        lastError_ = "Mod is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outMod.id = obj->getProperty("id");
    outMod.name = obj->getProperty("name").toString();
    outMod.type = static_cast<ModType>(static_cast<int>(obj->getProperty("type")));
    outMod.enabled = obj->getProperty("enabled");
    outMod.rate = obj->getProperty("rate");
    outMod.waveform = static_cast<LFOWaveform>(static_cast<int>(obj->getProperty("waveform")));
    outMod.phase = obj->getProperty("phase");
    outMod.phaseOffset = obj->getProperty("phaseOffset");
    outMod.value = obj->getProperty("value");
    outMod.tempoSync = obj->getProperty("tempoSync");
    outMod.syncDivision =
        static_cast<SyncDivision>(static_cast<int>(obj->getProperty("syncDivision")));
    outMod.triggerMode =
        static_cast<LFOTriggerMode>(static_cast<int>(obj->getProperty("triggerMode")));
    outMod.oneShot = obj->getProperty("oneShot");
    outMod.useLoopRegion = obj->getProperty("useLoopRegion");
    outMod.loopStart = obj->getProperty("loopStart");
    outMod.loopEnd = obj->getProperty("loopEnd");
    outMod.midiChannel = obj->getProperty("midiChannel");
    outMod.midiNote = obj->getProperty("midiNote");
    if (obj->hasProperty("audioAttackMs"))
        outMod.audioAttackMs = obj->getProperty("audioAttackMs");
    if (obj->hasProperty("audioReleaseMs"))
        outMod.audioReleaseMs = obj->getProperty("audioReleaseMs");
    outMod.curvePreset =
        static_cast<CurvePreset>(static_cast<int>(obj->getProperty("curvePreset")));

    // Curve points
    auto curvePointsVar = obj->getProperty("curvePoints");
    if (curvePointsVar.isArray()) {
        auto* arr = curvePointsVar.getArray();
        for (const auto& pointVar : *arr) {
            if (!pointVar.isObject())
                continue;
            auto* pointObj = pointVar.getDynamicObject();
            CurvePointData point;
            point.phase = pointObj->getProperty("phase");
            point.value = pointObj->getProperty("value");
            point.tension = pointObj->getProperty("tension");
            outMod.curvePoints.push_back(point);
        }
    }

    // Links
    auto linksVar = obj->getProperty("links");
    if (linksVar.isArray()) {
        auto* arr = linksVar.getArray();
        for (const auto& linkVar : *arr) {
            if (!linkVar.isObject())
                continue;
            auto* linkObj = linkVar.getDynamicObject();
            ModLink link;
            auto targetVar = linkObj->getProperty("target");
            if (targetVar.isObject()) {
                auto* targetObj = targetVar.getDynamicObject();
                link.target.deviceId = targetObj->getProperty("deviceId");
                link.target.paramIndex = targetObj->getProperty("paramIndex");
            }
            link.amount = linkObj->getProperty("amount");
            if (linkObj->hasProperty("bipolar"))
                link.bipolar = static_cast<bool>(linkObj->getProperty("bipolar"));
            outMod.links.push_back(link);
        }
    }

    // Legacy target/amount
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        outMod.target.deviceId = targetObj->getProperty("deviceId");
        outMod.target.paramIndex = targetObj->getProperty("paramIndex");
    }
    outMod.amount = obj->getProperty("amount");

    return true;
}

juce::var ProjectSerializer::serializeParameterInfo(const ParameterInfo& param) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("paramIndex", param.paramIndex);
    obj->setProperty("name", param.name);
    obj->setProperty("unit", param.unit);
    obj->setProperty("minValue", param.minValue);
    obj->setProperty("maxValue", param.maxValue);
    obj->setProperty("defaultValue", param.defaultValue);
    obj->setProperty("currentValue", param.currentValue);
    obj->setProperty("scale", static_cast<int>(param.scale));
    obj->setProperty("skewFactor", param.skewFactor);
    obj->setProperty("modulatable", param.modulatable);
    obj->setProperty("bipolarModulation", param.bipolarModulation);

    // Choices
    juce::Array<juce::var> choicesArray;
    for (const auto& choice : param.choices) {
        choicesArray.add(choice);
    }
    obj->setProperty("choices", juce::var(choicesArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeParameterInfo(const juce::var& json, ParameterInfo& outParam) {
    if (!json.isObject()) {
        lastError_ = "Parameter is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outParam.paramIndex = obj->getProperty("paramIndex");
    outParam.name = obj->getProperty("name").toString();
    outParam.unit = obj->getProperty("unit").toString();
    outParam.minValue = obj->getProperty("minValue");
    outParam.maxValue = obj->getProperty("maxValue");
    outParam.defaultValue = obj->getProperty("defaultValue");
    outParam.currentValue = obj->getProperty("currentValue");
    outParam.scale = static_cast<ParameterScale>(static_cast<int>(obj->getProperty("scale")));
    outParam.skewFactor = obj->getProperty("skewFactor");
    outParam.modulatable = obj->getProperty("modulatable");
    outParam.bipolarModulation = obj->getProperty("bipolarModulation");

    // Choices
    auto choicesVar = obj->getProperty("choices");
    if (choicesVar.isArray()) {
        auto* arr = choicesVar.getArray();
        for (const auto& choiceVar : *arr) {
            outParam.choices.push_back(choiceVar.toString());
        }
    }

    return true;
}

// ============================================================================
// Utility functions
// ============================================================================

juce::String ProjectSerializer::colourToString(const juce::Colour& colour) {
    return colour.toDisplayString(true);  // ARGB hex string
}

juce::Colour ProjectSerializer::stringToColour(const juce::String& str) {
    return juce::Colour::fromString(str);
}

juce::String ProjectSerializer::makeRelativePath(const juce::File& projectFile,
                                                 const juce::File& targetFile) {
    return targetFile.getRelativePathFrom(projectFile.getParentDirectory());
}

juce::File ProjectSerializer::resolveRelativePath(const juce::File& projectFile,
                                                  const juce::String& relativePath) {
    return projectFile.getParentDirectory().getChildFile(relativePath);
}

}  // namespace magda
