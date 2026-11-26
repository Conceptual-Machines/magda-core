// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProjectSerializer.hpp"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

#include "../../audio/plugins/DeviceStateHydration.hpp"
#include "../../core/AddressedParameters.hpp"
#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/ClipOperations.hpp"
#include "../../core/DeviceParamMigrations.hpp"
#include "../../core/DrumGridPads.hpp"
#include "../../core/LegacyDeviceAliases.hpp"
#include "../../core/OpenProjectAddressing.hpp"
#include "../../core/PadChannels.hpp"
#include "../../core/PadPathMigration.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/ViewModeState.hpp"
#include "../ProjectManager.hpp"
#include "DawProjectArchive.hpp"
#include "JsonReader.hpp"
#include "NativeProjectDocumentAdapter.hpp"
#include "ProjectMigration.hpp"
#include "core/SamplerDevices.hpp"

namespace magda {

thread_local juce::String ProjectSerializer::lastError_;
thread_local ProjectLoadFailure ProjectSerializer::lastLoadFailure_ = ProjectLoadFailure::None;

// ============================================================================
// File I/O with gzip compression
// ============================================================================

namespace {

/// Every pad holding a device gets its channel, numbered after the staged ids.
void migratePadChannels(std::vector<TrackInfo>& tracks) {
    TrackId nextTrackId = 1;
    for (const auto& track : tracks)
        nextTrackId = std::max(nextTrackId, track.id + 1);
    pad_channels::adoptChannelNames(tracks);
    pad_channels::sync(tracks, nextTrackId);
}

/// Written relative by this project, not an absolute path from another platform: a
/// macOS "/Volumes/..." path is not absolute to Windows, and must not be re-rooted.
bool isProjectRelative(const juce::String& path) {
    return path.isNotEmpty() && !path.startsWithChar('/') && !path.startsWithChar('\\') &&
           !path.startsWithChar('~') && !(path.length() > 1 && path[1] == ':');
}

/// @p path as saved in @p projectFile: relative when it is inside the project's folder.
juce::String toProjectPath(const juce::File& projectFile, const juce::String& path) {
    if (projectFile == juce::File() || !juce::File::isAbsolutePath(path))
        return path;
    const juce::File file(path);
    if (!file.isAChildOf(projectFile.getParentDirectory()))
        return path;
    return file.getRelativePathFrom(projectFile.getParentDirectory()).replaceCharacter('\\', '/');
}

/// @p path as read from @p projectFile: a relative one resolves against its folder.
juce::String fromProjectPath(const juce::File& projectFile, const juce::String& path) {
    if (projectFile == juce::File() || !isProjectRelative(path))
        return path;
    return projectFile.getParentDirectory().getChildFile(path).getFullPathName();
}

/// Take paths are the clip serializer's to write; they follow the same rule as sources.
void mapTakePaths(juce::var& clips, const std::function<juce::String(const juce::String&)>& map) {
    for (auto& clip : clips.isArray() ? *clips.getArray() : juce::Array<juce::var>{}) {
        const auto takes = clip["audio"]["takes"];
        for (auto& take : takes.isArray() ? *takes.getArray() : juce::Array<juce::var>{})
            if (auto* object = take.getDynamicObject())
                object->setProperty("filePath", map(object->getProperty("filePath").toString()));
    }
}

/// A Sampler's files are the `source` of its device state and of each of its zones, at any
/// depth of the chain.
void mapSamplerSources(const juce::var& tree,
                       const std::function<juce::String(const juce::String&)>& map) {
    if (const auto* array = tree.getArray()) {
        for (const auto& item : *array)
            mapSamplerSources(item, map);
        return;
    }
    auto* object = tree.getDynamicObject();
    if (object == nullptr)
        return;
    if (sampler_devices::isSampler(object->getProperty("pluginId").toString())) {
        if (auto doc = device_state::decode(object->getProperty("pluginState").toString())) {
            bool changed = false;
            const auto remap = [&map, &changed](device_state::Node& node, const char* key) {
                const auto path = node.props[key].toString();
                const auto mapped = map(path);
                if (path.isNotEmpty() && mapped != path) {
                    node.props.set(key, mapped);
                    changed = true;
                }
            };
            remap(doc->root, "source");
            remap(doc->root, "instrument");
            for (auto& child : doc->root.children)
                if (child.type == "zone")
                    remap(child, "source");
            if (changed)
                object->setProperty("pluginState", device_state::encode(*doc));
        }
    }
    for (const auto& property : object->getProperties())
        mapSamplerSources(property.value, map);
}

}  // namespace

bool ProjectSerializer::saveToFile(const juce::File& file, const ProjectInfo& info) {
    try {
        auto parentDir = file.getParentDirectory();
        if (!parentDir.createDirectory()) {
            lastError_ = "Failed to create project file directory: " + parentDir.getFullPathName();
            return false;
        }

        // Serialize to JSON
        auto json = serializeProject(info, file);

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
        lastError_ = std::move(error);
        return false;
    }

    return true;
}

bool ProjectSerializer::loadDawProjectAndStage(const juce::File& file, StagedProjectData& outData,
                                               const juce::File& audioExtractionDir) {
    return loadDawProjectAndStage(file, outData, audioExtractionDir,
                                  ProjectManager::captureCreationSettingsFromConfig());
}

bool ProjectSerializer::loadDawProjectAndStage(const juce::File& file, StagedProjectData& outData,
                                               const juce::File& audioExtractionDir,
                                               const ProjectCreationSettings& creationSettings) {
    ProjectDocument document;
    juce::String error;

    if (!DawProjectArchive::readFromFile(file, document, error, audioExtractionDir,
                                         &creationSettings.defaults)) {
        lastError_ = std::move(error);
        return false;
    }

    outData = NativeProjectDocumentAdapter::toStagedProjectData(document);
    // DAWproject has no MAGDA defaults block. An import is a new, unsaved
    // project, so snapshot this installation's new-project preferences once.
    outData.info.timelineLengthBars = creationSettings.timelineLengthBars;
    outData.info.defaults = creationSettings.defaults;
    return true;
}

namespace {

/// Give every pad device with no id one, from a counter past every DeviceId the
/// staged project already names.
///
/// Self-contained: staging has no TrackManager behind it, and the ids only have
/// to be unique within the project. `refreshIdCountersFromTracks` reads them at
/// commit and moves the live counter past them (#2207).
void allocateStagedPadDeviceIds(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack) {
    DeviceId next = 0;
    std::vector<DeviceInfo*> unkeyed;

    std::function<void(std::vector<ChainElement>&)> walk =
        [&](std::vector<ChainElement>& elements) {
            for (auto& element : elements) {
                if (isRack(element)) {
                    for (auto& chain : getRack(element).chains)
                        walk(chain.elements);
                    continue;
                }

                auto& device = getDevice(element);
                next = std::max(next, device.id + 1);

                if (!device.pads)
                    continue;

                stampPadRackId(device);
                for (auto& pad : device.pads->chains) {
                    for (auto& padElement : pad.elements) {
                        if (!isDevice(padElement))
                            continue;
                        auto& padDevice = getDevice(padElement);
                        if (padDevice.id == INVALID_DEVICE_ID)
                            unkeyed.push_back(&padDevice);
                        else
                            next = std::max(next, padDevice.id + 1);
                    }
                }
            }
        };

    const auto walkTrack = [&](TrackInfo& track) {
        walk(track.chain.fxChainElements);
        for (auto& element : track.chain.postFxChainElements)
            next = std::max(next, element.device.id + 1);
        for (auto& element : track.chain.mixerAnalysisElements)
            next = std::max(next, element.device.id + 1);
    };

    for (auto& track : tracks)
        walkTrack(track);
    if (masterTrack != nullptr)
        walkTrack(*masterTrack);

    for (auto* device : unkeyed)
        device->id = next++;
}

/**
 * Give old/unassigned Session clips one deterministic slot and make sure every
 * referenced scene has durable metadata. A second clip saved into an occupied
 * slot is migrated to the first free row on that track rather than remaining
 * an ambiguous grid collision.
 */
void reconcileProjectScenes(ProjectInfo& info, std::vector<ClipInfo>& clips) {
    ensureProjectSceneCount(info, 1);

    std::vector<ClipInfo*> sessionClips;
    for (auto& clip : clips) {
        if (clip.view == ClipView::Session)
            sessionClips.push_back(&clip);
        else
            clip.sceneIndex = -1;
    }
    std::ranges::sort(sessionClips, {}, [](const ClipInfo* clip) {
        return std::tuple{clip->trackId, clip->id};
    });

    std::map<TrackId, std::set<int>> occupied;
    for (auto* clip : sessionClips) {
        auto& trackSlots = occupied[clip->trackId];
        auto sceneIndex = clip->sceneIndex;
        if (sceneIndex < 0 || trackSlots.contains(sceneIndex)) {
            sceneIndex = 0;
            while (trackSlots.contains(sceneIndex))
                ++sceneIndex;
            clip->sceneIndex = sceneIndex;
        }
        trackSlots.insert(sceneIndex);
        ensureProjectSceneCount(info, sceneIndex + 1);
    }
}

}  // namespace

bool ProjectSerializer::loadAndStage(const juce::File& file, StagedProjectData& outData) {
    return loadAndStage(file, outData, ProjectManager::captureCreationSettingsFromConfig());
}

bool ProjectSerializer::loadAndStage(const juce::File& file, StagedProjectData& outData,
                                     const ProjectCreationSettings& creationSettings) {
    lastLoadFailure_ = ProjectLoadFailure::Unreadable;
    try {
        if (!file.existsAsFile()) {
            lastError_ = "File does not exist: " + file.getFullPathName();
            return false;
        }

        juce::FileInputStream inputStream(file);
        if (!inputStream.openedOk()) {
            lastError_ = "Failed to open file for reading: " + file.getFullPathName();
            return false;
        }

        juce::GZIPDecompressorInputStream gzipStream(inputStream);
        const auto jsonString = gzipStream.readEntireStreamAsString();

        juce::var json;
        if (const auto parsed = juce::JSON::parse(jsonString, json); parsed.failed()) {
            lastError_ = "Not valid JSON: " + parsed.getErrorMessage();
            return false;
        }

        return stageProject(json, file, creationSettings, outData);

    } catch (const std::exception& e) {
        lastError_ = "Exception while loading: " + juce::String(e.what());
        return false;
    } catch (...) {
        lastError_ = "Unknown exception while loading";
        return false;
    }
}

bool ProjectSerializer::stageProject(const juce::var& json, const juce::File& projectFile,
                                     const ProjectCreationSettings& creationSettings,
                                     StagedProjectData& outData) {
    outData = StagedProjectData{};
    ser::DecodeLog log;
    const ser::ObjectReader doc(json, {}, log);
    const auto malformed = [&log] {
        lastError_ = log.summary();
        lastLoadFailure_ = ProjectLoadFailure::Malformed;
        return false;
    };
    if (!doc.isValid())
        return malformed();

    // Before anything else reads the document: a newer one must not be read as this one.
    int schemaVersion = 1;  // v1 wrote no key
    if (doc.optional("schemaVersion", schemaVersion) && schemaVersion < 1)
        doc.fail("schemaVersion", "not a schema version: " + juce::String(schemaVersion));
    if (!log.ok())
        return malformed();
    if (schemaVersion > kProjectSchemaVersion) {
        lastError_ = "Saved with project schema " + juce::String(schemaVersion) +
                     "; this build reads up to " + juce::String(kProjectSchemaVersion);
        lastLoadFailure_ = ProjectLoadFailure::NewerSchema;
        return false;
    }

    auto& info = outData.info;
    if (doc.required("magdaVersion", info.version) && info.version.isEmpty())
        doc.fail("magdaVersion", "empty");
    juce::String createdAt;
    doc.optional("createdAt", createdAt);
    info.createdAt = juce::Time::fromISO8601(createdAt);
    if (juce::String lastModified;
        doc.optional("lastModified", lastModified) && lastModified.isNotEmpty())
        info.lastModified = juce::Time::fromISO8601(lastModified);
    // Internal recovery metadata. Ordinary project files omit it.
    doc.optional("autosaveMediaDirectory", info.autosaveMediaDirectory);
    readProjectSettings(project_migration::upgradeProjectSettings(doc.object("project")), info,
                        creationSettings);

    const auto fromProject = [&projectFile](const juce::String& path) {
        return fromProjectPath(projectFile, path);
    };
    mapSamplerSources(doc.get("tracks"), fromProject);
    mapSamplerSources(doc.get("masterTrack"), fromProject);
    doc.forEachObject(
        "tracks",
        [&outData](const ser::ObjectReader& item) {
            TrackInfo track;
            read(item, track);
            outData.tracks.push_back(std::move(track));
        },
        true);

    // Parsed only. Installing them is a commit-phase job: this can run on a background
    // thread, and the still-open project's clips are reading the live pool meanwhile.
    readSources(doc, outData.sources, projectFile);

    auto clips = doc.get("clips");
    mapTakePaths(clips, fromProject);
    doc.forEachObject(
        "clips",
        [&outData](const ser::ObjectReader& item) {
            ClipInfo clip;
            read(item, clip, outData.info.tempo, &outData.legacySources);
            outData.clips.push_back(std::move(clip));
        },
        true);

    readAutomation(doc, outData.automationLanes, outData.automationClips);

    if (const auto master = doc.optionalObject("masterTrack")) {
        auto track = std::make_unique<TrackInfo>();
        read(*master, *track);
        outData.masterTrack = std::move(track);
    }

    if (!log.ok())
        return malformed();

    reconcileProjectScenes(info, outData.clips);

    // Retired stock Tracktion effects load as their compiled successors. Runs on the
    // staged model, so the project is already canonical by the time anything is committed
    // or re-saved.
    legacy_devices::migrateRetiredDevicesInProject(outData.tracks, outData.masterTrack.get(),
                                                   outData.automationLanes,
                                                   outData.automationClips);

    // Then the Chord Engine's declared role (#2427): created as a MIDI device, it produces
    // none. Touches nothing the parameter migrations read.
    legacy_devices::normalizeChordEngineRoleInProject(outData.tracks, outData.masterTrack.get());

    // Then any device that renumbered its parameters, so a saved link still addresses the
    // parameter it was made against (#2079). After the aliases: a rewritten device is
    // already its successor by the time its indices are looked up.
    device_param_migrations::applyParamIndexMigrations(outData.tracks, outData.masterTrack.get(),
                                                       outData.automationLanes,
                                                       outData.automationClips);
    // Then any choice list that grew, before its saved length is lost.
    device_param_migrations::applyChoiceGrowths(outData.tracks, outData.masterTrack.get(),
                                                outData.automationLanes, outData.automationClips);

    // Then an id for every pad device saved before pads had them, on the staged model:
    // a pad with no id cannot be keyed, so anything reading a staged project without
    // committing it would see a Drum Grid with no pads (#2207).
    allocateStagedPadDeviceIds(outData.tracks, outData.masterTrack.get());

    // Then the pad addresses saved before pad ownership was a step type. After the ids,
    // because a pad chain is matched by the id of the device that owns it (#2219).
    pad_paths::migrateLegacyPadPaths(outData.tracks, outData.masterTrack.get(),
                                     outData.automationLanes);

    // Then the retired duplicate parameter record (#2317), once, before either engine
    // projection is built. After the aliases and index migrations so ids are current.
    daw::audio::device_state_hydration::hydrateStagedProject(
        outData.tracks, outData.masterTrack.get(), info.version);

    // Then the pads' mixer channels (#3010), which is also where a pad on a bus moves onto
    // one. After the pad ids, which the channels link by.
    migratePadChannels(outData.tracks);

    // Opaque pass-throughs to AliasRegistry and BindingRegistry.
    if (doc.has("paramAliases"))
        info.paramAliases = doc.get("paramAliases");
    if (doc.has("projectBindings"))
        info.projectBindings = doc.get("projectBindings");

    lastError_.clear();
    lastLoadFailure_ = ProjectLoadFailure::None;
    return true;
}

void ProjectSerializer::readProjectSettings(const ser::ObjectReader& in, ProjectInfo& info,
                                            const ProjectCreationSettings& creationSettings) {
    in.required("name", info.name);
    in.required("tempo", info.tempo);
    std::vector<int> timeSignature;
    in.readArray("timeSignature", timeSignature, true);
    if (timeSignature.size() >= 2) {
        info.timeSignatureNumerator = timeSignature[0];
        info.timeSignatureDenominator = timeSignature[1];
    } else if (in.has("timeSignature")) {
        in.fail("timeSignature", "expected a numerator and a denominator");
    }
    in.required("projectLength", info.projectLength);
    in.optional("sampleRate", info.sampleRate);
    info.timelineLengthBars = creationSettings.timelineLengthBars;
    in.optional("timelineLengthBars", info.timelineLengthBars);
    in.optional("savedWithEngine", info.savedWithEngine);
    in.optional("renderBitDepth", info.renderBitDepth);
    in.optional("bounceBitDepth", info.bounceBitDepth);
    in.optional("keyRoot", info.keyRoot);
    in.optional("keyQuality", info.keyQuality);

    // An old project has no defaults object: the current new-project preferences stand in.
    info.defaults = creationSettings.defaults;
    if (const auto defaults = in.optionalObject("defaults")) {
        auto& d = info.defaults;
        defaults->optional("zoomViewBars", d.zoomViewBars);
        defaults->optional("autoCrossfade", d.autoCrossfade);
        defaults->optional("overlapPlaysBoth", d.overlapPlaysBoth);
        defaults->optional("chordPreview", d.chordPreview);
        defaults->optional("postFxPostFader", d.postFxPostFader);
        defaults->optional("clipColourMode", d.clipColourMode);
        if (defaults->has("colourPalette")) {
            std::vector<ProjectColourEntry> palette;
            defaults->forEachObject("colourPalette", [&palette](const ser::ObjectReader& item) {
                ProjectColourEntry entry;
                entry.name = "Colour " + juce::String(static_cast<int>(palette.size()) + 1);
                // An unreadable colour keeps its slot with the default colour.
                if (juce::String colour; item.optional("colour", colour) && colour.length() == 8 &&
                                         colour.containsOnly("0123456789abcdefABCDEF"))
                    entry.colour = juce::Colour::fromString(colour).getARGB();
                if (juce::String name; item.optional("name", name) && name.isNotEmpty())
                    entry.name = name;
                palette.push_back(entry);
            });
            d.colourPalette = std::move(palette);
        }
    }

    // Absent until a project is credited, so no object means no credits.
    info.metadata = {};
    if (const auto metadata = in.optionalObject("metadata"))
        for (const auto& field : kProjectMetadataFields)
            metadata->optional(field.key, info.metadata.*field.member);

    // A project from before scenes were saved keeps the eight historical rows.
    if (in.has("scenes")) {
        info.scenes.clear();
        info.nextSceneId = 1;
        std::set<SceneId> ids;
        in.forEachObject("scenes", [&info, &ids](const ser::ObjectReader& item) {
            ProjectScene scene;
            if (!item.required("id", scene.id) || scene.id < 0 || !ids.insert(scene.id).second)
                return;
            item.optional("name", scene.name);
            if (juce::Colour colour; item.optional("colour", colour))
                scene.colourArgb = colour.getARGB();
            info.nextSceneId = std::max(info.nextSceneId, scene.id + 1);
            info.scenes.push_back(std::move(scene));
        });
        if (SceneId next = 0; in.optional("nextSceneId", next))
            info.nextSceneId = std::max(info.nextSceneId, next);
        ensureProjectSceneCount(info, 1);
    }

    info.markers.clear();
    in.forEachObject("markers", [&info](const ser::ObjectReader& item) {
        ProjectTimelineMarker marker;
        item.required("id", marker.id);
        item.required("positionBeats", marker.positionBeats);
        item.required("name", marker.name);
        if (juce::Colour colour; item.optional("colour", colour))
            marker.colourArgb = colour.getARGB();
        if (marker.id > 0)
            info.markers.push_back(marker);
    });

    if (const auto loop = in.optionalObject("loop")) {
        loop->required("enabled", info.loopEnabled);
        loop->required("startBeats", info.loopStartBeats);
        loop->required("endBeats", info.loopEndBeats);
    }
    if (const auto zoom = in.optionalObject("zoom")) {
        zoom->required("horizontalZoom", info.horizontalZoom);
        zoom->required("verticalZoom", info.verticalZoom);
        zoom->required("scrollX", info.scrollX);
        zoom->required("scrollY", info.scrollY);
    }
    in.optional("activeView", info.activeView);
}

void ProjectSerializer::commitStaged(StagedProjectData& data) {
    // Before the clips, which reference these by id.
    installStagedSources(data.sources, data.legacySources, data.clips);
    ClipOperations::wrapFreeLoops(data.clips, data.info.tempo);

    commitStagedData(data.tracks, data.clips, data.automationLanes, data.automationClips);

    // After, so re-probing a source can rescale the events that point at it.
    resolveStagedSources(data.sources);

    // Restore master track chain elements (plugins on the master bus)
    if (data.masterTrack) {
        auto& tm = TrackManager::getInstance();
        auto* masterTrack = tm.getTrack(MASTER_TRACK_ID);
        if (masterTrack) {
            tm.setTrackAudioOutput(MASTER_TRACK_ID, data.masterTrack->audioOutputDevice);
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
            // Notify listeners so audio bridge creates plugins for master devices
            tm.notifyTrackDevicesChanged(MASTER_TRACK_ID);
        }
    }
}

// ============================================================================
// Project-level serialization
// ============================================================================

juce::var ProjectSerializer::serializeProject(const ProjectInfo& info,
                                              const juce::File& projectFile) {
    auto* obj = new juce::DynamicObject();

    // Version and metadata
    obj->setProperty("magdaVersion", info.version);
    obj->setProperty("schemaVersion", kProjectSchemaVersion);
    obj->setProperty("lastModified", info.lastModified.toISO8601(true));
    obj->setProperty("createdAt", info.createdAt.toISO8601(true));
    if (info.autosaveMediaDirectory.isNotEmpty())
        obj->setProperty("autosaveMediaDirectory", info.autosaveMediaDirectory);

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

    auto* defaultsObj = new juce::DynamicObject();
    defaultsObj->setProperty("zoomViewBars", info.defaults.zoomViewBars);
    defaultsObj->setProperty("autoCrossfade", info.defaults.autoCrossfade);
    defaultsObj->setProperty("overlapPlaysBoth", info.defaults.overlapPlaysBoth);
    defaultsObj->setProperty("chordPreview", info.defaults.chordPreview);
    defaultsObj->setProperty("postFxPostFader", info.defaults.postFxPostFader);
    defaultsObj->setProperty("clipColourMode", info.defaults.clipColourMode);
    juce::Array<juce::var> colourPalette;
    for (const auto& entry : info.defaults.colourPalette) {
        auto* entryObj = new juce::DynamicObject();
        entryObj->setProperty("colour", juce::Colour(entry.colour).toDisplayString(true));
        entryObj->setProperty("name", entry.name);
        colourPalette.add(juce::var(entryObj));
    }
    defaultsObj->setProperty("colourPalette", juce::var(colourPalette));
    projectObj->setProperty("defaults", juce::var(defaultsObj));

    projectObj->setProperty("savedWithEngine", info.savedWithEngine);
    projectObj->setProperty("renderBitDepth", info.renderBitDepth);
    projectObj->setProperty("bounceBitDepth", info.bounceBitDepth);
    projectObj->setProperty("keyRoot", info.keyRoot);
    projectObj->setProperty("keyQuality", info.keyQuality);

    // Title and credits. Only the fields that were filled in are written, so a
    // project nobody has credited carries no "metadata" object at all.
    if (!info.metadata.isEmpty()) {
        auto* metadataObj = new juce::DynamicObject();
        for (const auto& field : kProjectMetadataFields) {
            const auto& value = info.metadata.*field.member;
            if (value.isNotEmpty())
                metadataObj->setProperty(field.key, value);
        }
        projectObj->setProperty("metadata", juce::var(metadataObj));
    }

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

    juce::Array<juce::var> scenesArray;
    for (const auto& scene : info.scenes) {
        auto* sceneObj = new juce::DynamicObject();
        sceneObj->setProperty("id", scene.id);
        sceneObj->setProperty("name", scene.name);
        sceneObj->setProperty("colour", colourToString(juce::Colour(scene.colourArgb)));
        scenesArray.add(juce::var(sceneObj));
    }
    projectObj->setProperty("scenes", juce::var(scenesArray));
    projectObj->setProperty("nextSceneId", info.nextSceneId);

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
    // Only what something addresses of a hosted plugin's values: its chunk
    // carries the rest (#2636).
    const auto addressed = addressedInOpenProject();

    const auto tracks = serializeTracks(addressed);
    mapSamplerSources(tracks, [&projectFile](const juce::String& path) {
        return toProjectPath(projectFile, path);
    });
    obj->setProperty("tracks", tracks);
    obj->setProperty("sources", serializeSources(projectFile));
    const auto toProject = [&projectFile](const juce::String& path) {
        return toProjectPath(projectFile, path);
    };
    auto clips = serializeClips();
    mapTakePaths(clips, toProject);
    obj->setProperty("clips", clips);
    obj->setProperty("automation", serializeAutomation());

    // Serialize master track separately: its chain holds the master bus plugins, and it names
    // the device pair the master plays to (#3424).
    auto* masterTrack = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
    if (masterTrack && (!masterTrack->chain.fxChainElements.empty() ||
                        !masterTrack->chain.postFxChainElements.empty() ||
                        !masterTrack->chain.mixerAnalysisElements.empty() ||
                        masterTrack->audioOutputDevice.isNotEmpty())) {
        auto saved = *masterTrack;
        dropUnaddressedHostedParameters(saved, addressed);
        auto master = serializeTrackInfo(saved);
        mapSamplerSources(master, [&projectFile](const juce::String& path) {
            return toProjectPath(projectFile, path);
        });
        obj->setProperty("masterTrack", master);
    }

    // Parameter aliases (UserProject layer -- opaque pass-through)
    if (!info.paramAliases.isVoid())
        obj->setProperty("paramAliases", info.paramAliases);

    // Project-scope bindings (opaque pass-through)
    if (!info.projectBindings.isVoid())
        obj->setProperty("projectBindings", info.projectBindings);

    return {obj};
}

bool ProjectSerializer::deserializeProject(const juce::var& json, ProjectInfo& outInfo) {
    StagedProjectData staged;
    if (!stageProject(json, {}, ProjectManager::captureCreationSettingsFromConfig(), staged))
        return false;

    outInfo = staged.info;
    commitStaged(staged);
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

        // Drop the previous project's tracks. What listened for the runtime gap
        // here now hears ProjectManagerListener::projectTeardown() instead
        // (#2576), so this is a plain clear rather than a signal.
        trackManager.clearAllTracks();

        // Restore tracks in their own batch that closes BEFORE clips are
        // restored. Closing the track batch fires notifyTracksChanged() ->
        // AudioBridge::syncAll(), which is what actually creates the engine
        // tracks (they are built lazily during plugin sync, not by
        // restoreTrack). Clips must sync into existing tracks: ClipSynchronizer
        // bails with "no track" if the track isn't there yet, silently dropping
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
        }  // trackBatch closes here: engine tracks now exist.

        // Restore clips (synced into the now-existing tracks when clipBatch
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

juce::var ProjectSerializer::serializeTracks(const AddressedParameters& addressed) {
    juce::Array<juce::var> tracksArray;

    auto& trackManager = TrackManager::getInstance();
    for (const auto& track : trackManager.getTracks()) {
        auto saved = track;
        dropUnaddressedHostedParameters(saved, addressed);
        tracksArray.add(serializeTrackInfo(saved));
    }

    return {tracksArray};
}

juce::var ProjectSerializer::serializeSources(const juce::File& projectFile) {
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
        sourceObj->setProperty("filePath", toProjectPath(projectFile, source.filePath));
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
    return {sourcesArray};
}

bool ProjectSerializer::deserializeSourcesToStaging(const juce::var& json, std::vector<Source>& out,
                                                    const juce::File& projectFile) {
    auto* holder = new juce::DynamicObject();
    holder->setProperty("sources", json);
    return ser::decodeDocument(
        juce::var(holder), {}, lastError_,
        [&](const ser::ObjectReader& doc) { readSources(doc, out, projectFile); });
}

void ProjectSerializer::readSources(const ser::ObjectReader& doc, std::vector<Source>& out,
                                    const juce::File& projectFile) {
    doc.forEachObject("sources", [&out, &projectFile](const ser::ObjectReader& item) {
        Source source;
        item.required("id", source.id);
        if (juce::String path; item.required("filePath", path))
            source.filePath = fromProjectPath(projectFile, path);
        item.required("durationSeconds", source.durationSeconds);
        item.required("sampleRate", source.sampleRate);
        item.optional("detectedBpm", source.detectedBpm);
        item.optional("detectedKeyRoot", source.detectedKeyRoot);
        item.optional("detectedKeyScale", source.detectedKeyScale);
        out.push_back(std::move(source));
    });
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
    // By id, because the manager stores clips in an unordered_map and its
    // iteration order depends on the order they were inserted in. Without this,
    // opening a project and saving it rewrites the clips array in a different
    // order every time - the file changes with nothing in the project changing,
    // and no diff of two saves means anything.
    auto clips = clipManager.getClips();
    std::ranges::sort(clips, {}, &ClipInfo::id);

    for (const auto& clip : clips) {
        clipsArray.add(serializeClipInfo(clip));
    }

    return {clipsArray};
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

    return {obj};
}

// ============================================================================
// Component-level deserialization
// ============================================================================

void ProjectSerializer::readAutomation(const ser::ObjectReader& doc,
                                       std::vector<AutomationLaneInfo>& outLanes,
                                       std::vector<AutomationClipInfo>& outClips) {
    outLanes.clear();
    outClips.clear();
    // Absent in projects saved before automation existed.
    const auto automation = project_migration::upgradeAutomation(doc.get("automation"));
    if (automation.isVoid())
        return;

    const ser::ObjectReader in(automation, doc.pathOf("automation"), doc.log());
    in.forEachObject("lanes", [&outLanes](const ser::ObjectReader& item) {
        AutomationLaneInfo lane;
        read(item, lane);
        outLanes.push_back(std::move(lane));
    });
    in.forEachObject("clips", [&outClips](const ser::ObjectReader& item) {
        AutomationClipInfo clip;
        read(item, clip);
        outClips.push_back(std::move(clip));
    });
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
