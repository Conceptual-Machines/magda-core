#include "ProjectSerializer.hpp"

#include <juce_data_structures/juce_data_structures.h>

#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/ViewModeState.hpp"

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

        if (projectObj->hasProperty("sampleRate"))
            outData.info.sampleRate = projectObj->getProperty("sampleRate");
        if (projectObj->hasProperty("keyRoot"))
            outData.info.keyRoot = projectObj->getProperty("keyRoot");
        if (projectObj->hasProperty("keyQuality"))
            outData.info.keyQuality = projectObj->getProperty("keyQuality");

        // Loop settings
        auto loopVar = projectObj->getProperty("loop");
        if (loopVar.isObject()) {
            auto* loopObj = loopVar.getDynamicObject();
            outData.info.loopEnabled = loopObj->getProperty("enabled");
            outData.info.loopStartBeats = loopObj->getProperty("startBeats");
            outData.info.loopEndBeats = loopObj->getProperty("endBeats");
        }

        // Zoom/scroll state
        auto zoomVar = projectObj->getProperty("zoom");
        if (zoomVar.isObject()) {
            auto* zoomObj = zoomVar.getDynamicObject();
            outData.info.horizontalZoom = zoomObj->getProperty("horizontalZoom");
            outData.info.verticalZoom = zoomObj->getProperty("verticalZoom");
            outData.info.scrollX = zoomObj->getProperty("scrollX");
            outData.info.scrollY = zoomObj->getProperty("scrollY");
            DBG("ZOOM DESERIALIZE (loadAndStage): hz=" << outData.info.horizontalZoom
                                                       << " scrollX=" << outData.info.scrollX
                                                       << " scrollY=" << outData.info.scrollY);
        } else {
            DBG("ZOOM DESERIALIZE (loadAndStage): no zoom object in project JSON");
        }

        // Active view mode
        if (projectObj->hasProperty("activeView")) {
            outData.info.activeView = static_cast<int>(projectObj->getProperty("activeView"));
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
    projectObj->setProperty("sampleRate", info.sampleRate);
    projectObj->setProperty("keyRoot", info.keyRoot);
    projectObj->setProperty("keyQuality", info.keyQuality);

    // Loop settings
    auto* loopObj = new juce::DynamicObject();
    loopObj->setProperty("enabled", info.loopEnabled);
    loopObj->setProperty("startBeats", info.loopStartBeats);
    loopObj->setProperty("endBeats", info.loopEndBeats);
    projectObj->setProperty("loop", juce::var(loopObj));

    // Zoom/scroll state
    DBG("ZOOM SERIALIZE: info.horizontalZoom=" << info.horizontalZoom << " scrollX=" << info.scrollX
                                               << " scrollY=" << info.scrollY);
    if (info.horizontalZoom > 0.0) {
        auto* zoomObj = new juce::DynamicObject();
        zoomObj->setProperty("horizontalZoom", info.horizontalZoom);
        zoomObj->setProperty("verticalZoom", info.verticalZoom);
        zoomObj->setProperty("scrollX", info.scrollX);
        zoomObj->setProperty("scrollY", info.scrollY);
        projectObj->setProperty("zoom", juce::var(zoomObj));
        DBG("ZOOM SERIALIZE: wrote zoom object to JSON");
    } else {
        DBG("ZOOM SERIALIZE: skipped (horizontalZoom <= 0)");
    }

    // Active view mode
    if (info.activeView != 1) {  // Only save if not default (Arrange)
        projectObj->setProperty("activeView", info.activeView);
    }

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

    if (projectObj->hasProperty("sampleRate"))
        outInfo.sampleRate = projectObj->getProperty("sampleRate");
    if (projectObj->hasProperty("keyRoot"))
        outInfo.keyRoot = projectObj->getProperty("keyRoot");
    if (projectObj->hasProperty("keyQuality"))
        outInfo.keyQuality = projectObj->getProperty("keyQuality");

    // Loop settings
    auto loopVar = projectObj->getProperty("loop");
    if (loopVar.isObject()) {
        auto* loopObj = loopVar.getDynamicObject();
        outInfo.loopEnabled = loopObj->getProperty("enabled");
        outInfo.loopStartBeats = loopObj->getProperty("startBeats");
        outInfo.loopEndBeats = loopObj->getProperty("endBeats");
    }

    // Zoom/scroll state
    auto zoomVar = projectObj->getProperty("zoom");
    if (zoomVar.isObject()) {
        auto* zoomObj = zoomVar.getDynamicObject();
        outInfo.horizontalZoom = zoomObj->getProperty("horizontalZoom");
        outInfo.verticalZoom = zoomObj->getProperty("verticalZoom");
        outInfo.scrollX = zoomObj->getProperty("scrollX");
        outInfo.scrollY = zoomObj->getProperty("scrollY");
        DBG("ZOOM DESERIALIZE: hz=" << outInfo.horizontalZoom << " scrollX=" << outInfo.scrollX
                                    << " scrollY=" << outInfo.scrollY);
    } else {
        DBG("ZOOM DESERIALIZE: no zoom object in project JSON");
    }

    // Active view mode
    if (projectObj->hasProperty("activeView")) {
        outInfo.activeView = static_cast<int>(projectObj->getProperty("activeView"));
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
