#include "ProjectSerializer.hpp"

#include <juce_data_structures/juce_data_structures.h>

#include <unordered_set>

#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/LegacyDeviceAliases.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/ViewModeState.hpp"
#include "DawProjectArchive.hpp"
#include "NativeProjectDocumentAdapter.hpp"

namespace magda {

thread_local juce::String ProjectSerializer::lastError_;

// ============================================================================
// File I/O with gzip compression
// ============================================================================

bool ProjectSerializer::saveToFile(const juce::File& file, const ProjectInfo& info) {
    try {
        auto parentDir = file.getParentDirectory();
        if (!parentDir.createDirectory()) {
            lastError_ = "Failed to create project file directory: " + parentDir.getFullPathName();
            return false;
        }

        // Serialize to JSON
        auto json = serializeProject(info);

        // Convert to pretty-printed string
        juce::String jsonString = juce::JSON::toString(json, true);

        // Use temporary file for atomic/crash-safe writing
        // Write to temp file first, then atomically replace destination
        juce::TemporaryFile tempFile(file);

        // Scope the output streams so file handles are closed before the rename.
        // Windows does not allow moving/renaming a file with an open handle.
        {
            juce::FileOutputStream outputStream(tempFile.getFile());
            if (!outputStream.openedOk()) {
                lastError_ = "Failed to open temporary file for writing: " +
                             tempFile.getFile().getFullPathName();
                return false;
            }

            juce::GZIPCompressorOutputStream gzipStream(outputStream, 9);  // Max compression
            // Write plain UTF-8 JSON text (no JUCE binary length prefix)
            gzipStream.writeText(jsonString, false, false, nullptr);
            gzipStream.flush();
            outputStream.flush();
        }

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

bool ProjectSerializer::exportToDawProject(const juce::File& file, const ProjectInfo& info) {
    auto document = NativeProjectDocumentAdapter::captureCurrentProject(info);
    juce::String error;

    if (!DawProjectArchive::writeToFile(file, document, error)) {
        lastError_ = error;
        return false;
    }

    return true;
}

bool ProjectSerializer::loadDawProjectAndStage(const juce::File& file, StagedProjectData& outData,
                                               const juce::File& audioExtractionDir) {
    ProjectDocument document;
    juce::String error;

    if (!DawProjectArchive::readFromFile(file, document, error, audioExtractionDir)) {
        lastError_ = error;
        return false;
    }

    outData = NativeProjectDocumentAdapter::toStagedProjectData(document);
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
        if (projectObj->hasProperty("timelineLengthBars"))
            outData.info.timelineLengthBars = projectObj->getProperty("timelineLengthBars");
        if (projectObj->hasProperty("renderBitDepth"))
            outData.info.renderBitDepth = projectObj->getProperty("renderBitDepth");
        if (projectObj->hasProperty("bounceBitDepth"))
            outData.info.bounceBitDepth = projectObj->getProperty("bounceBitDepth");
        if (projectObj->hasProperty("keyRoot"))
            outData.info.keyRoot = projectObj->getProperty("keyRoot");
        if (projectObj->hasProperty("keyQuality"))
            outData.info.keyQuality = projectObj->getProperty("keyQuality");

        // Named timeline markers
        outData.info.markers.clear();
        auto markersVar = projectObj->getProperty("markers");
        if (markersVar.isArray()) {
            auto* markersArray = markersVar.getArray();
            for (const auto& markerVar : *markersArray) {
                if (!markerVar.isObject())
                    continue;

                auto* markerObj = markerVar.getDynamicObject();
                ProjectTimelineMarker marker;
                marker.id = static_cast<int>(markerObj->getProperty("id"));
                marker.positionBeats = static_cast<double>(markerObj->getProperty("positionBeats"));
                marker.name = markerObj->getProperty("name").toString();

                auto colourString = markerObj->getProperty("colour").toString();
                if (colourString.isNotEmpty())
                    marker.colourArgb = stringToColour(colourString).getARGB();

                if (marker.id > 0)
                    outData.info.markers.push_back(marker);
            }
        }

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

        // Parsed only. Installing them is a commit-phase job: this runs on a
        // background thread, the staging steps below can still fail, and the
        // still-open project's clips are reading the live pool meanwhile.
        deserializeSourcesToStaging(obj->getProperty("sources"), outData.sources);

        if (!deserializeClipsToStaging(obj->getProperty("clips"), outData.clips, outData.info.tempo,
                                       &outData.legacySources)) {
            return false;
        }

        if (!deserializeAutomationToStaging(obj->getProperty("automation"), outData.automationLanes,
                                            outData.automationClips)) {
            return false;
        }

        // Deserialize master track if present (backward-compatible: old projects won't have it)
        auto masterTrackVar = obj->getProperty("masterTrack");
        if (masterTrackVar.isObject()) {
            auto mt = std::make_unique<TrackInfo>();
            if (deserializeTrackInfo(masterTrackVar, *mt)) {
                outData.masterTrack = std::move(mt);
            } else {
                DBG("WARNING: Failed to deserialize masterTrack data - master plugins will be "
                    "lost");
            }
        }

        // Retired stock Tracktion effects load as their compiled successors.
        // Runs on the staged model, so the project is already canonical by the
        // time anything is committed or re-saved.
        legacy_devices::migrateRetiredDevicesInProject(outData.tracks, outData.masterTrack.get(),
                                                       outData.automationLanes,
                                                       outData.automationClips);

        // Parameter aliases (UserProject layer -- opaque pass-through to AliasRegistry)
        if (obj->hasProperty("paramAliases"))
            outData.info.paramAliases = obj->getProperty("paramAliases");

        // Project-scope bindings (opaque pass-through to BindingRegistry)
        if (obj->hasProperty("projectBindings"))
            outData.info.projectBindings = obj->getProperty("projectBindings");

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
    // Before the clips, which reference these by id.
    installStagedSources(data.sources, data.legacySources, data.clips);

    commitStagedData(data.tracks, data.clips, data.automationLanes, data.automationClips);

    // After, so re-probing a source can rescale the events that point at it.
    resolveStagedSources(data.sources);

    // Restore master track chain elements (plugins on the master bus)
    if (data.masterTrack) {
        auto& tm = TrackManager::getInstance();
        auto* masterTrack = tm.getTrack(MASTER_TRACK_ID);
        if (masterTrack) {
            masterTrack->chain.fxChainElements = std::move(data.masterTrack->chain.fxChainElements);
            masterTrack->chain.postFxChainElements =
                std::move(data.masterTrack->chain.postFxChainElements);
            masterTrack->chain.mixerAnalysisElements =
                std::move(data.masterTrack->chain.mixerAnalysisElements);
            // Update device ID counter to include master chain devices
            for (const auto& element : masterTrack->chain.fxChainElements) {
                if (isDevice(element))
                    tm.ensureDeviceIdAbove(getDevice(element).id);
            }
            for (const auto& element : masterTrack->chain.postFxChainElements)
                tm.ensurePostFxDeviceIdAbove(element.device.id);
            for (const auto& element : masterTrack->chain.mixerAnalysisElements)
                tm.ensureMixerAnalysisDeviceIdAbove(element.device.id);
            // Notify listeners so audio bridge creates TE plugins for master devices
            tm.notifyTrackDevicesChanged(MASTER_TRACK_ID);
        }
    }
}

// ============================================================================
// Project-level serialization
// ============================================================================

juce::var ProjectSerializer::serializeProject(const ProjectInfo& info) {
    auto* obj = new juce::DynamicObject();

    // Version and metadata
    obj->setProperty("magdaVersion", info.version);
    obj->setProperty("schemaVersion", kProjectSchemaVersion);
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
    projectObj->setProperty("timelineLengthBars", info.timelineLengthBars);
    projectObj->setProperty("renderBitDepth", info.renderBitDepth);
    projectObj->setProperty("bounceBitDepth", info.bounceBitDepth);
    projectObj->setProperty("keyRoot", info.keyRoot);
    projectObj->setProperty("keyQuality", info.keyQuality);

    // Named timeline markers
    if (!info.markers.empty()) {
        juce::Array<juce::var> markersArray;
        for (const auto& marker : info.markers) {
            auto* markerObj = new juce::DynamicObject();
            markerObj->setProperty("id", marker.id);
            markerObj->setProperty("positionBeats", marker.positionBeats);
            markerObj->setProperty("name", marker.name);
            markerObj->setProperty("colour", colourToString(juce::Colour(marker.colourArgb)));
            markersArray.add(juce::var(markerObj));
        }
        projectObj->setProperty("markers", juce::var(markersArray));
    }

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

    // Serialize tracks, clips, and automation. Sources go before clips: a clip's
    // events reference them by id (#1901).
    obj->setProperty("tracks", serializeTracks());
    obj->setProperty("sources", serializeSources());
    obj->setProperty("clips", serializeClips());
    obj->setProperty("automation", serializeAutomation());

    // Serialize master track separately (its chain elements hold master bus plugins)
    auto* masterTrack = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
    if (masterTrack && (!masterTrack->chain.fxChainElements.empty() ||
                        !masterTrack->chain.postFxChainElements.empty() ||
                        !masterTrack->chain.mixerAnalysisElements.empty())) {
        obj->setProperty("masterTrack", serializeTrackInfo(*masterTrack));
    }

    // Parameter aliases (UserProject layer -- opaque pass-through)
    if (!info.paramAliases.isVoid())
        obj->setProperty("paramAliases", info.paramAliases);

    // Project-scope bindings (opaque pass-through)
    if (!info.projectBindings.isVoid())
        obj->setProperty("projectBindings", info.projectBindings);

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
    if (projectObj->hasProperty("timelineLengthBars"))
        outInfo.timelineLengthBars = projectObj->getProperty("timelineLengthBars");
    if (projectObj->hasProperty("renderBitDepth"))
        outInfo.renderBitDepth = projectObj->getProperty("renderBitDepth");
    if (projectObj->hasProperty("bounceBitDepth"))
        outInfo.bounceBitDepth = projectObj->getProperty("bounceBitDepth");
    if (projectObj->hasProperty("keyRoot"))
        outInfo.keyRoot = projectObj->getProperty("keyRoot");
    if (projectObj->hasProperty("keyQuality"))
        outInfo.keyQuality = projectObj->getProperty("keyQuality");

    // Named timeline markers
    outInfo.markers.clear();
    auto markersVar = projectObj->getProperty("markers");
    if (markersVar.isArray()) {
        auto* markersArray = markersVar.getArray();
        for (const auto& markerVar : *markersArray) {
            if (!markerVar.isObject())
                continue;

            auto* markerObj = markerVar.getDynamicObject();
            ProjectTimelineMarker marker;
            marker.id = static_cast<int>(markerObj->getProperty("id"));
            marker.positionBeats = static_cast<double>(markerObj->getProperty("positionBeats"));
            marker.name = markerObj->getProperty("name").toString();

            auto colourString = markerObj->getProperty("colour").toString();
            if (colourString.isNotEmpty())
                marker.colourArgb = stringToColour(colourString).getARGB();

            if (marker.id > 0)
                outInfo.markers.push_back(marker);
        }
    }

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

    std::vector<Source> stagedSources;
    deserializeSourcesToStaging(obj->getProperty("sources"), stagedSources);

    std::vector<Source> stagedLegacySources;
    if (!deserializeClipsToStaging(obj->getProperty("clips"), stagedClips, outInfo.tempo,
                                   &stagedLegacySources)) {
        return false;  // Failed - no state modified
    }

    if (!deserializeAutomationToStaging(obj->getProperty("automation"), stagedAutomation,
                                        stagedAutomationClips)) {
        return false;  // Failed - no state modified
    }

    // The master track is staged here rather than after the commit below, so
    // the migration pass sees it: a lane targeting a retired effect on the
    // master has to be pruned in the same pass that rewrites the device, or it
    // survives holding a paramIndex into a parameter list that no longer exists.
    auto masterTrackVar = obj->getProperty("masterTrack");
    TrackInfo stagedMasterTrack;
    const bool hasMasterTrack =
        masterTrackVar.isObject() && deserializeTrackInfo(masterTrackVar, stagedMasterTrack);

    // Retired stock Tracktion effects load as their compiled successors.
    legacy_devices::migrateRetiredDevicesInProject(stagedTracks,
                                                   hasMasterTrack ? &stagedMasterTrack : nullptr,
                                                   stagedAutomation, stagedAutomationClips);

    // Stage 2: All components validated successfully - now commit to managers atomically
    installStagedSources(stagedSources, stagedLegacySources, stagedClips);
    commitStagedData(stagedTracks, stagedClips, stagedAutomation, stagedAutomationClips);
    resolveStagedSources(stagedSources);

    // Restore master track chain elements (plugins on the master bus)
    if (hasMasterTrack) {
        auto& tm = TrackManager::getInstance();
        if (auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
            masterTrack->chain.fxChainElements = std::move(stagedMasterTrack.chain.fxChainElements);
            masterTrack->chain.postFxChainElements =
                std::move(stagedMasterTrack.chain.postFxChainElements);
            masterTrack->chain.mixerAnalysisElements =
                std::move(stagedMasterTrack.chain.mixerAnalysisElements);
            for (const auto& element : masterTrack->chain.fxChainElements) {
                if (isDevice(element))
                    tm.ensureDeviceIdAbove(getDevice(element).id);
            }
            for (const auto& element : masterTrack->chain.postFxChainElements)
                tm.ensurePostFxDeviceIdAbove(element.device.id);
            for (const auto& element : masterTrack->chain.mixerAnalysisElements)
                tm.ensureMixerAnalysisDeviceIdAbove(element.device.id);
            tm.notifyTrackDevicesChanged(MASTER_TRACK_ID);
        }
    }

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

    // Coalesce the per-item structural notifications fired by the clear + restore
    // below into a single one per manager. Without this, restoring N tracks fires
    // notifyTracksChanged() N times, and each one rebuilds every track/mixer panel
    // AND re-runs AudioBridge::syncAll() over all tracks -- O(N^2) work that hangs
    // the UI for seconds on a large project. The batch fires one notification when
    // the scope closes.
    {
        ClipManager::BatchScope clipBatch;
        AutomationManager::BatchScope automationBatch;

        // Clear all existing data from managers
        clipManager.clearAllClips();
        automationManager.clearAll();

        // Tear down the previous project's tracks OUTSIDE the restore batch, so
        // clearAllTracks()'s notifyTracksChanged() fires immediately and runs
        // AudioBridge::syncAll() against an empty track set. That removes the old
        // TE AudioTracks and their synced plugins. If the clear is coalesced into
        // the restore batch below, syncAll only ever sees the FINAL state, so the
        // additive, path-keyed plugin sync finds Track[N] > Device[M] still
        // present -- track/device IDs reset to 1 every project, so the paths
        // collide across projects -- and skips recreating the plugin (it only
        // creates devices whose path is absent). The device then becomes a dead
        // husk: no editor, no processing. This is the "open A, open B, open A
        // again -> VST has no UI and does nothing" bug. clearAllTracks fires a
        // single teardown notification, so it does not reintroduce the O(N^2)
        // fan-out the restore batch guards against.
        trackManager.clearAllTracks();

        // Restore tracks in their own batch that closes BEFORE clips are
        // restored. Closing the track batch fires notifyTracksChanged() ->
        // AudioBridge::syncAll(), which is what actually creates the TE
        // AudioTracks (they are built lazily during plugin sync, not by
        // restoreTrack). Clips must sync into existing TE tracks: ClipSynchronizer
        // bails with "no TE track" if the track isn't there yet, silently dropping
        // the clip's MIDI/audio. Restoring clips first (the previous behaviour)
        // left arrangement clips out of the engine on load -> instruments silent.
        {
            TrackManager::BatchScope trackBatch;

            for (auto& track : stagedTracks) {
                trackManager.restoreTrack(track);
            }

            // After all tracks are restored, ensure TrackManager ID counters
            // (track/device/rack/chain) are updated to avoid ID collisions.
            trackManager.refreshIdCountersFromTracks();
        }  // trackBatch closes here: TE AudioTracks now exist.

        // Restore clips (synced into the now-existing TE tracks when clipBatch
        // closes at the end of this scope).
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
    }

    // Select the first track so the UI has a valid selection after load. Done
    // after the batch closes so panels exist and reflect the selection.
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

juce::var ProjectSerializer::serializeSources() {
    // Only sources some clip still references are written. This filters the
    // emitted snapshot and deliberately does NOT prune the live pool: the pool
    // is additive within a session precisely so an undone delete, or a paste
    // from the clipboard, still finds its source. Autosave runs this on a
    // timer, so pruning here would quietly break both between saves.
    std::unordered_set<SourceId> live;
    for (const auto& clip : ClipManager::getInstance().getClips()) {
        if (!clip.isAudio())
            continue;
        for (const auto& event : clip.audio().events)
            live.insert(event.sourceId);
    }

    juce::Array<juce::var> sourcesArray;
    for (const auto& source : SourcePool::getInstance().snapshot()) {
        if (live.count(source.id) == 0)
            continue;

        auto* sourceObj = new juce::DynamicObject();
        sourceObj->setProperty("id", source.id);
        sourceObj->setProperty("filePath", source.filePath);
        sourceObj->setProperty("durationSeconds", source.durationSeconds);
        sourceObj->setProperty("sampleRate", source.sampleRate);
        if (source.detectedBpm > 0.0)
            sourceObj->setProperty("detectedBpm", source.detectedBpm);
        if (!source.detectedKeyRoot.empty())
            sourceObj->setProperty("detectedKeyRoot", juce::String(source.detectedKeyRoot));
        if (!source.detectedKeyScale.empty())
            sourceObj->setProperty("detectedKeyScale", juce::String(source.detectedKeyScale));
        sourcesArray.add(juce::var(sourceObj));
    }
    return juce::var(sourcesArray);
}

void ProjectSerializer::deserializeSourcesToStaging(const juce::var& json,
                                                    std::vector<Source>& out) {
    if (!json.isArray())
        return;

    for (const auto& entry : *json.getArray()) {
        auto* sourceObj = entry.getDynamicObject();
        if (sourceObj == nullptr)
            continue;
        Source source;
        source.id = sourceObj->getProperty("id");
        source.filePath = sourceObj->getProperty("filePath").toString();
        source.durationSeconds = sourceObj->getProperty("durationSeconds");
        source.sampleRate = sourceObj->getProperty("sampleRate");
        source.detectedBpm = sourceObj->getProperty("detectedBpm");
        source.detectedKeyRoot = sourceObj->getProperty("detectedKeyRoot").toString().toStdString();
        source.detectedKeyScale =
            sourceObj->getProperty("detectedKeyScale").toString().toStdString();
        out.push_back(std::move(source));
    }
}

SourceId ProjectSerializer::stageLegacySource(std::vector<Source>& legacySources,
                                              const juce::String& filePath,
                                              double fallbackDuration) {
    for (auto& staged : legacySources) {
        if (staged.filePath == filePath) {
            // Several v1 clips can name one file with different recorded
            // durations. Keep the longest: a short one would clamp the other
            // clips' anchors and cap their right-extension.
            staged.durationSeconds = juce::jmax(staged.durationSeconds, fallbackDuration);
            return staged.id;
        }
    }

    Source staged;
    staged.id = -2 - static_cast<SourceId>(legacySources.size());  // never -1
    staged.filePath = filePath;
    staged.durationSeconds = fallbackDuration;
    legacySources.push_back(std::move(staged));
    return legacySources.back().id;
}

void ProjectSerializer::installStagedSources(const std::vector<Source>& sources,
                                             const std::vector<Source>& legacySources,
                                             std::vector<ClipInfo>& stagedClips) {
    auto& pool = SourcePool::getInstance();
    pool.clear();

    // insert() dedups by canonical path and returns the winner, so two entries
    // for one file (a v2 table entry plus a v1-migrated acquire of the same
    // path, or two saved paths differing only in case) collapse to one id. The
    // loser's events have to follow it or they dangle and lose their audio.
    struct Remap {
        SourceId owner;
        double loserRate;
    };
    std::unordered_map<SourceId, Remap> remapped;

    const auto installOne = [&](const Source& source) {
        const auto owner = pool.insert(source);
        if (owner != INVALID_SOURCE_ID && owner != source.id)
            remapped[source.id] = {owner, source.effectiveSampleRate()};
    };

    for (const auto& source : sources)
        installOne(source);

    // v1 sources were staged under provisional ids; this is where they become
    // real. Acquiring probes the file, so the rate can differ from the nominal
    // one the migration computed its anchors at.
    for (const auto& staged : legacySources) {
        const auto owner = pool.acquire(staged.filePath);
        if (owner == INVALID_SOURCE_ID)
            continue;

        if (auto* pooled = pool.getMutable(owner);
            pooled != nullptr && pooled->durationSeconds <= 0.0 && staged.durationSeconds > 0.0) {
            pooled->durationSeconds = staged.durationSeconds;
        }
        remapped[staged.id] = {owner, staged.effectiveSampleRate()};
    }

    if (remapped.empty())
        return;

    for (auto& clip : stagedClips) {
        if (!clip.isAudio())
            continue;
        for (auto& event : clip.audio().events) {
            const auto it = remapped.find(event.sourceId);
            if (it == remapped.end())
                continue;

            // The two entries name the same file but may have been probed at
            // different rates, so the sample counts move with the id.
            event.rescaleSourcePositions(it->second.loserRate, sourceRateOf(it->second.owner));
            event.sourceId = it->second.owner;
        }
    }
}

void ProjectSerializer::resolveStagedSources(const std::vector<Source>& sources) {
    auto& pool = SourcePool::getInstance();
    for (const auto& source : sources) {
        // A project saved while the file was missing carries sampleRate 0, so
        // its events' anchors were computed at the nominal rate. Re-probing now
        // resolves the source, and the pool's rate-change handler rescales
        // them: this runs after the clips are committed so those events exist.
        if (!source.isResolved())
            pool.resolveFacts(source.id);
    }
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
                                                  double projectTempo,
                                                  std::vector<Source>* legacySources) {
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
        if (!deserializeClipInfo(clipVar, clip, projectTempo, legacySources)) {
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
