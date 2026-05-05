#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;
using Catch::Approx;

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root.createDirectory();
    return root;
}

juce::File createTestTempFile(const juce::String& suffix) {
    return testTempRoot().getNonexistentChildFile("temp", suffix);
}

}  // namespace

// Test fixture to ensure clean state and temp file cleanup between tests
struct ProjectTestFixture {
    std::vector<juce::File> tempFiles;
    std::vector<juce::File> tempDirs;

    ProjectTestFixture() {
        // Clear all singleton state before each test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~ProjectTestFixture() {
        // Clean up temp directories (wrapper dirs created by saveProjectAs)
        for (auto& dir : tempDirs) {
            if (dir.isDirectory()) {
                dir.deleteRecursively();
            }
        }

        // Clean up temp files (even if test fails)
        for (auto& file : tempFiles) {
            if (file.existsAsFile()) {
                file.deleteFile();
            }
        }

        // Clean up singleton state after test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    // Helper to create unique temp file with automatic cleanup
    // suffix: The file extension/suffix to append (e.g., ".mgd")
    juce::File createTempFile(const juce::String& suffix) {
        auto file = createTestTempFile(suffix);
        tempFiles.push_back(file);
        return file;
    }

    // Returns the actual file path after saveProjectAs wraps it in a project directory.
    // e.g., /tmp/foo.mgd -> /tmp/foo/foo.mgd
    static juce::File wrappedPath(const juce::File& file) {
        auto projectName = file.getFileNameWithoutExtension();
        auto parentDir = file.getParentDirectory();
        if (parentDir.getFileName() != projectName) {
            auto wrapperDir = parentDir.getChildFile(projectName);
            return wrapperDir.getChildFile(file.getFileName());
        }
        return file;
    }

    // Create a temp file and register its wrapper directory for cleanup
    juce::File createTempProjectFile(const juce::String& suffix) {
        auto file = createTestTempFile(suffix);
        tempFiles.push_back(file);
        // Register the wrapper directory for cleanup
        auto wrapperDir =
            file.getParentDirectory().getChildFile(file.getFileNameWithoutExtension());
        tempDirs.push_back(wrapperDir);
        return file;
    }
};

TEST_CASE("Project Serialization Basics", "[project][serialization]") {
    ProjectTestFixture fixture;

    SECTION("Save and load empty project") {
        auto& projectManager = ProjectManager::getInstance();

        // Create unique temp file for testing
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        // Save empty project
        bool saved = projectManager.saveProjectAs(tempFile);
        INFO("saveProjectAs error: " << projectManager.getLastError());
        INFO("tempFile: " << tempFile.getFullPathName());
        REQUIRE(saved == true);
        REQUIRE(actualFile.existsAsFile() == true);

        // Load it back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Cleanup
    }

    SECTION("Project info serialization roundtrip") {
        ProjectInfo info;
        info.name = "Test Project";
        info.tempo = 128.0;
        info.timeSignatureNumerator = 3;
        info.timeSignatureDenominator = 4;
        info.loopEnabled = true;
        info.loopStartBeats = 4.0;
        info.loopEndBeats = 16.0;

        // Serialize to JSON
        auto json = ProjectSerializer::serializeProject(info);
        REQUIRE(json.isObject() == true);

        // Deserialize back
        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        // Verify fields
        REQUIRE(loaded.name == info.name);
        REQUIRE(loaded.tempo == info.tempo);
        REQUIRE(loaded.timeSignatureNumerator == info.timeSignatureNumerator);
        REQUIRE(loaded.timeSignatureDenominator == info.timeSignatureDenominator);
        REQUIRE(loaded.loopEnabled == info.loopEnabled);
        REQUIRE(loaded.loopStartBeats == info.loopStartBeats);
        REQUIRE(loaded.loopEndBeats == info.loopEndBeats);
    }
}

TEST_CASE("Audio clip serialization separates source facts from interpretation",
          "[project][serialization][audio]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Audio", TrackType::Audio);

    ClipInfo clip;
    clip.id = 42;
    clip.trackId = trackId;
    clip.name = "Loop";
    clip.setAudioContent();
    clip.setPlacementBeats(4.0, 16.0);
    clip.audio().source.filePath = "/tmp/loop.wav";
    clip.audio().source.durationSeconds = 2.7907;
    clip.audio().interpretation.bpm = 172.0;
    clip.audio().interpretation.totalBeats = 8.0;
    clip.audio().interpretation.totalBeatsLocked = true;
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 8.0;
    clip.loopLength = 8.0 * 60.0 / 172.0;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Audio Source Model";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);

    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);

    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    REQUIRE(clipObj->getProperty("audioSource").isVoid());

    auto* audioObj = clipObj->getProperty("audio").getDynamicObject();
    REQUIRE(audioObj != nullptr);
    auto* sourceObj = audioObj->getProperty("source").getDynamicObject();
    auto* interpretationObj = audioObj->getProperty("interpretation").getDynamicObject();
    auto* playbackObj = audioObj->getProperty("playback").getDynamicObject();
    REQUIRE(sourceObj != nullptr);
    REQUIRE(interpretationObj != nullptr);
    REQUIRE(playbackObj != nullptr);
    auto* placementObj = clipObj->getProperty("placement").getDynamicObject();
    REQUIRE(placementObj != nullptr);
    REQUIRE(static_cast<double>(sourceObj->getProperty("durationSeconds")) == Approx(2.7907));
    REQUIRE(static_cast<double>(interpretationObj->getProperty("totalBeats")) == Approx(8.0));
    REQUIRE(static_cast<bool>(interpretationObj->getProperty("totalBeatsLocked")));
    REQUIRE(static_cast<double>(playbackObj->getProperty("loopLengthBeats")) == Approx(8.0));
    REQUIRE(static_cast<double>(placementObj->getProperty("lengthBeats")) == Approx(16.0));

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->isAudio());
    REQUIRE(restored->placement.lengthBeats == Approx(16.0));
    REQUIRE(restored->audio().source.durationSeconds == Approx(2.7907));
    REQUIRE(restored->audio().interpretation.totalBeats == Approx(8.0));
    REQUIRE(restored->audio().interpretation.totalBeatsLocked);
    REQUIRE(restored->loopLengthBeats == Approx(8.0));
}

TEST_CASE("Clip serialization validates type and audio schema", "[project][serialization][audio]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Track", TrackType::Audio);

    ClipInfo clip;
    clip.id = 7;
    clip.trackId = trackId;
    clip.name = "Schema";
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 16.0);
    clip.audio().source.filePath = "/tmp/schema.wav";
    clip.audio().source.durationSeconds = 4.0;
    clip.audio().interpretation.bpm = 120.0;
    clip.audio().interpretation.totalBeats = 8.0;
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.loopLengthBeats = 8.0;
    clip.loopLength = 4.0;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Schema Validation";
    info.tempo = 120.0;

    SECTION("Unknown clip type is rejected") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->setProperty("type", 999);

        ProjectInfo loaded;
        REQUIRE_FALSE(ProjectSerializer::deserializeProject(json, loaded));
        REQUIRE(ProjectSerializer::getLastError().contains("Unknown clip type"));
    }

    SECTION("MIDI clip with audio payload is rejected") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->setProperty("type", static_cast<int>(ClipType::MIDI));

        ProjectInfo loaded;
        REQUIRE_FALSE(ProjectSerializer::deserializeProject(json, loaded));
        REQUIRE(ProjectSerializer::getLastError().contains("MIDI clip contains audio source data"));
    }

    SECTION("Legacy audioSource payload migrates to the audio model") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->removeProperty("audio");
        auto* sourceObj = new juce::DynamicObject();
        sourceObj->setProperty("filePath", "/tmp/legacy.wav");
        sourceObj->setProperty("offsetSeconds", 0.5);
        sourceObj->setProperty("offsetBeats", 1.0);
        sourceObj->setProperty("loopStartSeconds", 0.25);
        sourceObj->setProperty("loopLengthSeconds", 4.0);
        sourceObj->setProperty("loopStartBeats", 0.5);
        sourceObj->setProperty("loopLengthBeats", 8.0);
        sourceObj->setProperty("speedRatio", 1.25);
        sourceObj->setProperty("sourceNumBeats", 8.0);
        sourceObj->setProperty("sourceBPM", 120.0);
        sourceObj->setProperty("warpEnabled", true);
        auto* warpObj = new juce::DynamicObject();
        warpObj->setProperty("sourceTime", 1.0);
        warpObj->setProperty("warpTime", 1.25);
        juce::Array<juce::var> warpMarkers;
        warpMarkers.add(juce::var(warpObj));
        sourceObj->setProperty("warpMarkers", warpMarkers);
        clipObj->setProperty("audioSource", juce::var(sourceObj));

        ClipManager::getInstance().clearAllClips();

        ProjectInfo loaded;
        REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
        auto* restored = ClipManager::getInstance().getClip(clip.id);
        REQUIRE(restored != nullptr);
        REQUIRE(restored->isAudio());
        REQUIRE(restored->audio().source.filePath == "/tmp/legacy.wav");
        REQUIRE(restored->audio().source.durationSeconds == Approx(4.0));
        REQUIRE(restored->audio().interpretation.totalBeats == Approx(8.0));
        REQUIRE(restored->audio().interpretation.bpm == Approx(120.0));
        REQUIRE(restored->offset == Approx(0.5));
        REQUIRE(restored->offsetBeats == Approx(1.0));
        REQUIRE(restored->loopStart == Approx(0.25));
        REQUIRE(restored->loopLength == Approx(4.0));
        REQUIRE(restored->loopStartBeats == Approx(0.5));
        REQUIRE(restored->loopLengthBeats == Approx(8.0));
        REQUIRE(restored->speedRatio == Approx(1.25));
        REQUIRE(restored->warpEnabled);
        REQUIRE(restored->warpMarkers.size() == 1);
        REQUIRE(restored->warpMarkers.front().sourceTime == Approx(1.0));
        REQUIRE(restored->warpMarkers.front().warpTime == Approx(1.25));
    }
}

TEST_CASE("Project with Tracks", "[project][serialization][tracks]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with tracks") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        // Create a couple tracks
        trackManager.createTrack("Audio 1", TrackType::Audio);
        trackManager.createTrack("MIDI 1", TrackType::Audio);

        REQUIRE(trackManager.getTracks().size() == 2);

        // Create unique temp file
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        // Save
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear tracks
        trackManager.clearAllTracks();
        REQUIRE(trackManager.getTracks().size() == 0);

        // Load back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify tracks restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 2);
        REQUIRE(tracks[0].name == "Audio 1");
        REQUIRE(tracks[0].type == TrackType::Audio);
        REQUIRE(tracks[1].name == "MIDI 1");
        REQUIRE(tracks[1].type == TrackType::Audio);

        // Cleanup
        trackManager.clearAllTracks();
    }
}

TEST_CASE("Project File Format", "[project][serialization][file]") {
    ProjectTestFixture fixture;

    SECTION("File has .mgd extension") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(actualFile.hasFileExtension(".mgd") == true);
    }

    SECTION("File is not empty") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(actualFile.getSize() > 0);
    }
}

TEST_CASE("Project Manager State", "[project][manager]") {
    ProjectTestFixture fixture;

    SECTION("hasUnsavedChanges tracks dirty state") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create new project (should be clean)
        projectManager.newProject();
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Make a change
        trackManager.createTrack("Test", TrackType::Audio);
        projectManager.markDirty();

        REQUIRE(projectManager.hasUnsavedChanges() == true);

        // Save should clear dirty flag
        auto tempFile = fixture.createTempProjectFile(".mgd");

        REQUIRE(projectManager.saveProjectAs(tempFile) == true);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Cleanup
        trackManager.clearAllTracks();
    }

    SECTION("getCurrentProjectFile returns correct file") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);

        auto currentFile = projectManager.getCurrentProjectFile();
        REQUIRE(currentFile.getFullPathName() == actualFile.getFullPathName());
    }

    SECTION("hasOpenProject tracks project lifecycle correctly") {
        auto& projectManager = ProjectManager::getInstance();

        // Create new project - should be open even though clean and unsaved
        projectManager.newProject();
        REQUIRE(projectManager.hasOpenProject() == true);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Save project - should still be open
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);
        REQUIRE(projectManager.hasOpenProject() == true);

        // Close project - should not be open
        projectManager.closeProject();
        REQUIRE(projectManager.hasOpenProject() == false);

        // Load project - should be open again
        projectManager.loadProject(actualFile);
        REQUIRE(projectManager.hasOpenProject() == true);

        // Close again
        projectManager.closeProject();
        REQUIRE(projectManager.hasOpenProject() == false);

        // Cleanup
    }
}

TEST_CASE("Error Handling", "[project][serialization][errors]") {
    ProjectTestFixture fixture;

    SECTION("Load non-existent file fails gracefully") {
        auto& projectManager = ProjectManager::getInstance();

        auto nonExistentFile =
            testTempRoot().getNonexistentChildFile("this_does_not_exist", ".mgd");

        bool loaded = projectManager.loadProject(nonExistentFile);
        REQUIRE(loaded == false);
        REQUIRE(projectManager.getLastError().isNotEmpty() == true);
    }

    SECTION("Save to invalid path fails gracefully") {
        auto& projectManager = ProjectManager::getInstance();

        // Use a path inside a regular file (not a directory) so directory
        // creation fails — you can't create a subdirectory inside a file.
        auto blockingFile = testTempRoot().getChildFile("blocking_file_for_project_test");
        blockingFile.create();
        auto invalidFile = blockingFile.getChildFile("sub").getChildFile("test.mgd");

        bool saved = projectManager.saveProjectAs(invalidFile);
        REQUIRE(saved == false);

        // Cleanup
        blockingFile.deleteFile();
    }
}

TEST_CASE("Comprehensive Project Serialization", "[project][serialization][comprehensive]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with clips and devices") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();
        auto& clipManager = ClipManager::getInstance();

        // Create a track
        auto trackId = trackManager.createTrack("Test MIDI Track", TrackType::Audio);
        auto* track = trackManager.getTrack(trackId);
        REQUIRE(track != nullptr);

        // Add a device to the track
        DeviceInfo device;
        device.id = 1;
        device.name = "Test Synth";
        device.pluginId = "TestSynth";
        device.manufacturer = "Test";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;
        device.bypassed = false;
        trackManager.addDeviceToTrack(trackId, device);

        // Add a MIDI clip to the track
        auto clipId = clipManager.createMidiClip(trackId, 0.0, 4.0);

        // Get the clip and add some MIDI notes directly
        auto* clip = clipManager.getClip(clipId);
        REQUIRE(clip != nullptr);

        MidiNote note1;
        note1.noteNumber = 60;
        note1.velocity = 100;
        note1.startBeat = 0.0;
        note1.lengthBeats = 1.0;
        clip->midiNotes.push_back(note1);

        MidiNote note2;
        note2.noteNumber = 64;
        note2.velocity = 80;
        note2.startBeat = 1.0;
        note2.lengthBeats = 1.0;
        clip->midiNotes.push_back(note2);

        // Save the project
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();
        clipManager.clearAllClips();

        // Verify cleared
        REQUIRE(trackManager.getTracks().empty() == true);
        REQUIRE(clipManager.getClips().empty() == true);

        // Load the project back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify the track was restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].type == TrackType::Audio);

        // Verify the device was restored
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]) == true);
        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.name == "Test Synth");
        REQUIRE(restoredDevice.isInstrument == true);

        // Verify the clip was restored
        const auto& clips = clipManager.getClips();
        REQUIRE(clips.size() == 1);
        REQUIRE(clips[0].name == "MIDI 1");  // Default name from createMidiClip
        REQUIRE(clips[0].getType() == ClipType::MIDI);
        REQUIRE(clips[0].midiNotes.size() == 2);
        REQUIRE(clips[0].midiNotes[0].noteNumber == 60);
        REQUIRE(clips[0].midiNotes[1].noteNumber == 64);

        // Cleanup
    }

    SECTION("Save and load project with rack") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create a track
        auto trackId = trackManager.createTrack("Test Audio Track", TrackType::Audio);

        // Add a rack to the track
        auto rackId = trackManager.addRackToTrack(trackId, "Test Rack");
        REQUIRE(rackId != INVALID_RACK_ID);

        // Save the project
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();

        // Load the project back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify the track was restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);

        // Verify the rack was restored
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isRack(tracks[0].chainElements[0]) == true);
        const auto& restoredRack = getRack(tracks[0].chainElements[0]);
        REQUIRE(restoredRack.name == "Test Rack");

        // Cleanup
    }
}

TEST_CASE("Project metadata fields roundtrip", "[project][serialization][metadata]") {
    ProjectTestFixture fixture;

    SECTION("sampleRate, keyRoot, keyQuality serialize and deserialize") {
        ProjectInfo info;
        info.name = "Metadata Test";
        info.tempo = 140.0;
        info.sampleRate = 96000.0;
        info.keyRoot = 7;     // G
        info.keyQuality = 1;  // minor

        auto json = ProjectSerializer::serializeProject(info);
        REQUIRE(json.isObject() == true);

        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        REQUIRE(loaded.sampleRate == 96000.0);
        REQUIRE(loaded.keyRoot == 7);
        REQUIRE(loaded.keyQuality == 1);
    }

    SECTION("Missing metadata fields use defaults (backward compat)") {
        // Simulate an old project JSON without the new fields
        ProjectInfo info;
        info.name = "Old Project";
        info.tempo = 120.0;
        // Don't set sampleRate, keyRoot, keyQuality — use defaults

        auto json = ProjectSerializer::serializeProject(info);

        // Manually strip the new fields from the project object
        auto* rootObj = json.getDynamicObject();
        auto projectVar = rootObj->getProperty("project");
        auto* projectObj = projectVar.getDynamicObject();
        projectObj->removeProperty("sampleRate");
        projectObj->removeProperty("keyRoot");
        projectObj->removeProperty("keyQuality");

        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        // Should fall back to defaults
        REQUIRE(loaded.sampleRate == 44100.0);
        REQUIRE(loaded.keyRoot == -1);
        REQUIRE(loaded.keyQuality == 0);
    }
}

TEST_CASE("DeviceInfo pluginState roundtrip", "[project][serialization][pluginState]") {
    ProjectTestFixture fixture;

    SECTION("pluginState is serialized and deserialized") {
        auto& trackManager = TrackManager::getInstance();

        auto trackId = trackManager.createTrack("Plugin State Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "Test Plugin";
        device.pluginId = "TestPlugin";
        device.manufacturer = "TestCo";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;
        device.pluginState = "SGVsbG8gV29ybGQ=";  // base64 for "Hello World"
        trackManager.addDeviceToTrack(trackId, device);

        auto& projectManager = ProjectManager::getInstance();
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        trackManager.clearAllTracks();

        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]) == true);

        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.pluginState == juce::String("SGVsbG8gV29ybGQ="));
    }

    SECTION("Device without pluginState roundtrips with empty state") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        auto trackId = trackManager.createTrack("No State Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "No State Plugin";
        device.pluginId = "NoState";
        device.format = PluginFormat::Internal;
        // pluginState is empty by default
        trackManager.addDeviceToTrack(trackId, device);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);

        trackManager.clearAllTracks();
        REQUIRE(projectManager.loadProject(actualFile) == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]) == true);
        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.pluginState.isEmpty());
    }
}

TEST_CASE("RackInfo panel UI state roundtrip", "[project][serialization][rack][ui_state]") {
    RackInfo rack;
    rack.id = 7;
    rack.name = "Panel Rack";
    rack.expanded = false;
    rack.modPanelOpen = true;
    rack.paramPanelOpen = true;

    ChainInfo chain;
    chain.id = 8;
    chain.name = "Chain 1";
    rack.chains.push_back(std::move(chain));

    auto json = ProjectSerializer::serializeRackInfo(rack);

    RackInfo loaded;
    REQUIRE(ProjectSerializer::deserializeRackInfo(json, loaded));

    REQUIRE(loaded.id == rack.id);
    REQUIRE(loaded.name == rack.name);
    REQUIRE(loaded.expanded == false);
    REQUIRE(loaded.modPanelOpen == true);
    REQUIRE(loaded.paramPanelOpen == true);
    REQUIRE(loaded.chains.size() == 1);
}
