#include "ProjectSerializer.hpp"

#include <juce_data_structures/juce_data_structures.h>

#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"

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

        // Deserialize project
        return deserializeProject(json, outInfo);

    } catch (const std::exception& e) {
        lastError_ = "Exception while loading: " + juce::String(e.what());
        return false;
    } catch (...) {
        lastError_ = "Unknown exception while loading";
        return false;
    }
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
    loopObj->setProperty("start", info.loopStart);
    loopObj->setProperty("end", info.loopEnd);
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
        outInfo.loopStart = loopObj->getProperty("start");
        outInfo.loopEnd = loopObj->getProperty("end");
    }

    // ATOMIC DESERIALIZATION: Validate and stage ALL components before modifying any state.
    // This ensures that if any component fails to deserialize, we don't leave the project
    // in a partially-loaded, inconsistent state.

    // Stage 1: Deserialize all components into temporary collections (validation phase)
    std::vector<TrackInfo> stagedTracks;
    std::vector<ClipInfo> stagedClips;
    std::vector<AutomationLaneInfo> stagedAutomation;

    if (!deserializeTracksToStaging(obj->getProperty("tracks"), stagedTracks)) {
        return false;  // Failed - no state modified
    }

    if (!deserializeClipsToStaging(obj->getProperty("clips"), stagedClips)) {
        return false;  // Failed - no state modified
    }

    if (!deserializeAutomationToStaging(obj->getProperty("automation"), stagedAutomation)) {
        return false;  // Failed - no state modified
    }

    // Stage 2: All components validated successfully - now commit to managers atomically
    commitStagedData(stagedTracks, stagedClips, stagedAutomation);

    return true;
}

// ============================================================================
// Atomic commit of staged deserialization data
// ============================================================================

void ProjectSerializer::commitStagedData(std::vector<TrackInfo>& stagedTracks,
                                         std::vector<ClipInfo>& stagedClips,
                                         std::vector<AutomationLaneInfo>& stagedAutomation) {
    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();
    auto& automationManager = AutomationManager::getInstance();

    // Clear all existing data from managers
    trackManager.clearAllTracks();
    clipManager.clearAllClips();
    automationManager.clearAll();

    // Restore tracks
    // TODO: Performance - restoreTrack() calls notifyTracksChanged() for each track,
    // causing a notification storm for large projects. Consider adding batch restore
    // API to TrackManager that suppresses notifications during load and emits once at end.
    for (auto& track : stagedTracks) {
        trackManager.restoreTrack(track);
    }

    // After all tracks are restored, ensure TrackManager ID counters
    // (track/device/rack/chain) are updated to avoid ID collisions.
    trackManager.refreshIdCountersFromTracks();

    // Restore clips
    // TODO: Performance - restoreClip() calls notifyClipsChanged() for each clip,
    // causing a notification storm for large projects. Consider adding batch restore
    // mode to ClipManager that suppresses notifications during load and emits once at end.
    for (auto& clip : stagedClips) {
        clipManager.restoreClip(clip);
    }

    // Restore automation (currently always empty since deserialization is not implemented)
    for (auto& lane : stagedAutomation) {
        // TODO: Implement automation restoration when automation deserialization is implemented
        juce::ignoreUnused(lane);
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
    juce::Array<juce::var> lanesArray;

    auto& automationManager = AutomationManager::getInstance();
    for (const auto& lane : automationManager.getLanes()) {
        lanesArray.add(serializeAutomationLaneInfo(lane));
    }

    return juce::var(lanesArray);
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
                                                  std::vector<ClipInfo>& outClips) {
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
        if (!deserializeClipInfo(clipVar, clip)) {
            return false;  // Failed - staging vector discarded
        }
        outClips.push_back(std::move(clip));
    }

    return true;
}

bool ProjectSerializer::deserializeAutomationToStaging(const juce::var& json,
                                                       std::vector<AutomationLaneInfo>& outLanes) {
    // Bug Fix: Handle missing automation key gracefully for backward compatibility.
    // Older project files created before automation support won't have this key.
    if (json.isVoid()) {
        // No automation data present - treat as empty (backward compatible)
        outLanes.clear();
        return true;
    }

    if (!json.isArray()) {
        lastError_ = "Automation data is not an array";
        return false;
    }

    auto* arr = json.getArray();
    if (arr == nullptr) {
        lastError_ = "Automation data array is invalid";
        return false;
    }

    // To avoid silently losing user automation data, we treat the presence of
    // non-empty automation arrays as a hard load error until proper
    // deserialization is implemented or a forward-compat mechanism is added.
    if (!arr->isEmpty()) {
        lastError_ = "Project contains automation lanes, but automation deserialization "
                     "is not yet implemented. Cannot load project without losing automation data.";
        return false;
    }

    // Empty automation array is fine - no data to lose
    outLanes.clear();
    return true;
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

    // Routing
    obj->setProperty("midiInputDevice", track.midiInputDevice);
    obj->setProperty("midiOutputDevice", track.midiOutputDevice);
    obj->setProperty("audioInputDevice", track.audioInputDevice);
    obj->setProperty("audioOutputDevice", track.audioOutputDevice);

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

    // Routing
    outTrack.midiInputDevice = obj->getProperty("midiInputDevice").toString();
    outTrack.midiOutputDevice = obj->getProperty("midiOutputDevice").toString();
    outTrack.audioInputDevice = obj->getProperty("audioInputDevice").toString();
    outTrack.audioOutputDevice = obj->getProperty("audioOutputDevice").toString();

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
    obj->setProperty("internalLoopEnabled", clip.internalLoopEnabled);
    obj->setProperty("internalLoopLength", clip.internalLoopLength);
    obj->setProperty("sceneIndex", clip.sceneIndex);
    obj->setProperty("launchMode", static_cast<int>(clip.launchMode));
    obj->setProperty("launchQuantize", static_cast<int>(clip.launchQuantize));

    // Audio properties (flat model)
    if (clip.audioFilePath.isNotEmpty()) {
        obj->setProperty("audioFilePath", clip.audioFilePath);
        obj->setProperty("audioOffset", clip.audioOffset);
        obj->setProperty("audioStretchFactor", clip.audioStretchFactor);
        if (clip.detectedBPM > 0.0) {
            obj->setProperty("detectedBPM", clip.detectedBPM);
        }
    }

    // MIDI notes
    juce::Array<juce::var> midiNotesArray;
    for (const auto& note : clip.midiNotes) {
        midiNotesArray.add(serializeMidiNote(note));
    }
    obj->setProperty("midiNotes", juce::var(midiNotesArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeClipInfo(const juce::var& json, ClipInfo& outClip) {
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
    outClip.internalLoopEnabled = obj->getProperty("internalLoopEnabled");
    outClip.internalLoopLength = obj->getProperty("internalLoopLength");
    outClip.sceneIndex = obj->getProperty("sceneIndex");

    // Launch properties (backward compatible - defaults apply if missing)
    auto launchModeVar = obj->getProperty("launchMode");
    if (!launchModeVar.isVoid()) {
        outClip.launchMode = static_cast<LaunchMode>(static_cast<int>(launchModeVar));
    }
    auto launchQuantizeVar = obj->getProperty("launchQuantize");
    if (!launchQuantizeVar.isVoid()) {
        outClip.launchQuantize = static_cast<LaunchQuantize>(static_cast<int>(launchQuantizeVar));
    }

    // Audio properties (flat model)
    auto audioFilePathVar = obj->getProperty("audioFilePath");
    if (!audioFilePathVar.isVoid()) {
        // New flat format
        outClip.audioFilePath = audioFilePathVar.toString();
        outClip.audioOffset = obj->getProperty("audioOffset");
        outClip.audioStretchFactor = obj->getProperty("audioStretchFactor");
        if (outClip.audioStretchFactor <= 0.0)
            outClip.audioStretchFactor = 1.0;
        auto detectedBPMVar = obj->getProperty("detectedBPM");
        if (!detectedBPMVar.isVoid()) {
            outClip.detectedBPM = static_cast<double>(detectedBPMVar);
        }
    } else {
        // Migration from old audioSources format
        auto audioSourcesVar = obj->getProperty("audioSources");
        if (audioSourcesVar.isArray()) {
            auto* arr = audioSourcesVar.getArray();
            if (arr && !arr->isEmpty()) {
                auto firstSourceVar = (*arr)[0];
                if (firstSourceVar.isObject()) {
                    auto* srcObj = firstSourceVar.getDynamicObject();
                    outClip.audioFilePath = srcObj->getProperty("filePath").toString();
                    outClip.audioOffset = srcObj->getProperty("offset");
                    outClip.audioStretchFactor = srcObj->getProperty("stretchFactor");
                    if (outClip.audioStretchFactor <= 0.0)
                        outClip.audioStretchFactor = 1.0;
                }
            }
        }
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
