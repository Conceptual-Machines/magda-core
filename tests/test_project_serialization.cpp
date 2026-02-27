#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/PluginStateUtils.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// Test fixture to ensure clean state and temp file cleanup between tests
struct ProjectTestFixture {
    std::vector<juce::File> tempFiles;

    ProjectTestFixture() {
        // Clear all singleton state before each test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~ProjectTestFixture() {
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
        auto file = juce::File::createTempFile(suffix);
        tempFiles.push_back(file);
        return file;
    }
};

TEST_CASE("Project Serialization Basics", "[project][serialization]") {
    ProjectTestFixture fixture;

    SECTION("Save and load empty project") {
        auto& projectManager = ProjectManager::getInstance();

        // Create unique temp file for testing
        auto tempFile = fixture.createTempFile(".mgd");

        // Save empty project
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(tempFile.existsAsFile() == true);

        // Load it back
        bool loaded = projectManager.loadProject(tempFile);
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

TEST_CASE("Project with Tracks", "[project][serialization][tracks]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with tracks") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        // Create a couple tracks
        auto track1 = trackManager.createTrack("Audio 1", TrackType::Audio);
        auto track2 = trackManager.createTrack("MIDI 1", TrackType::MIDI);

        REQUIRE(trackManager.getTracks().size() == 2);

        // Create unique temp file
        auto tempFile = fixture.createTempFile(".mgd");

        // Save
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear tracks
        trackManager.clearAllTracks();
        REQUIRE(trackManager.getTracks().size() == 0);

        // Load back
        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded == true);

        // Verify tracks restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 2);
        REQUIRE(tracks[0].name == "Audio 1");
        REQUIRE(tracks[0].type == TrackType::Audio);
        REQUIRE(tracks[1].name == "MIDI 1");
        REQUIRE(tracks[1].type == TrackType::MIDI);

        // Cleanup
        trackManager.clearAllTracks();
    }
}

TEST_CASE("Project File Format", "[project][serialization][file]") {
    ProjectTestFixture fixture;

    SECTION("File has .mgd extension") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempFile(".mgd");

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(tempFile.hasFileExtension(".mgd") == true);
    }

    SECTION("File is not empty") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempFile(".mgd");

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(tempFile.getSize() > 0);
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
        auto tempFile = fixture.createTempFile(".mgd");

        projectManager.saveProjectAs(tempFile);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Cleanup
        trackManager.clearAllTracks();
    }

    SECTION("getCurrentProjectFile returns correct file") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempFile(".mgd");
        projectManager.saveProjectAs(tempFile);

        auto currentFile = projectManager.getCurrentProjectFile();
        REQUIRE(currentFile.getFullPathName() == tempFile.getFullPathName());
    }

    SECTION("hasOpenProject tracks project lifecycle correctly") {
        auto& projectManager = ProjectManager::getInstance();

        // Create new project - should be open even though clean and unsaved
        projectManager.newProject();
        REQUIRE(projectManager.hasOpenProject() == true);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Save project - should still be open
        auto tempFile = fixture.createTempFile(".mgd");
        projectManager.saveProjectAs(tempFile);
        REQUIRE(projectManager.hasOpenProject() == true);

        // Close project - should not be open
        projectManager.closeProject();
        REQUIRE(projectManager.hasOpenProject() == false);

        // Load project - should be open again
        projectManager.loadProject(tempFile);
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
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("this_does_not_exist_" +
                              juce::String(juce::Random::getSystemRandom().nextInt()) + ".mgd");

        bool loaded = projectManager.loadProject(nonExistentFile);
        REQUIRE(loaded == false);
        REQUIRE(projectManager.getLastError().isNotEmpty() == true);
    }

    SECTION("Save to invalid path fails gracefully") {
        auto& projectManager = ProjectManager::getInstance();

        // Use platform-independent method to create invalid parent directory
        auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        auto invalidParentDir = tempDir.getChildFile("nonexistent_parent_dir_for_project_test");
        if (invalidParentDir.exists()) {
            invalidParentDir.deleteRecursively();
        }
        auto invalidFile = invalidParentDir.getChildFile("test.mgd");

        bool saved = projectManager.saveProjectAs(invalidFile);
        REQUIRE(saved == false);
    }
}

TEST_CASE("Comprehensive Project Serialization", "[project][serialization][comprehensive]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with clips and devices") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();
        auto& clipManager = ClipManager::getInstance();

        // Create a track
        auto trackId = trackManager.createTrack("Test MIDI Track", TrackType::MIDI);
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
        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();
        clipManager.clearAllClips();

        // Verify cleared
        REQUIRE(trackManager.getTracks().empty() == true);
        REQUIRE(clipManager.getClips().empty() == true);

        // Load the project back
        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded == true);

        // Verify the track was restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].type == TrackType::MIDI);

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
        REQUIRE(clips[0].type == ClipType::MIDI);
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
        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();

        // Load the project back
        bool loaded = projectManager.loadProject(tempFile);
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

// ============================================================================
// Plugin State Serialization Tests
// ============================================================================

TEST_CASE("PluginStateUtils compress/decompress roundtrip", "[project][serialization][pluginstate]") {
    SECTION("Empty data") {
        std::vector<uint8_t> empty;
        auto compressed = PluginStateUtils::compress(empty);
        REQUIRE(compressed.empty());

        auto decompressed = PluginStateUtils::decompress(compressed);
        REQUIRE(decompressed.empty());
    }

    SECTION("Small payload roundtrip") {
        std::vector<uint8_t> original = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x80, 0x7F};
        auto compressed = PluginStateUtils::compress(original);
        REQUIRE(!compressed.empty());

        auto decompressed = PluginStateUtils::decompress(compressed);
        REQUIRE(decompressed == original);
    }

    SECTION("Larger payload roundtrip (simulated plugin state)") {
        // Simulate a realistic plugin state (~4 KB of structured data)
        std::vector<uint8_t> original(4096);
        juce::Random rng(42);
        for (auto& byte : original) {
            byte = static_cast<uint8_t>(rng.nextInt(256));
        }

        auto compressed = PluginStateUtils::compress(original);
        REQUIRE(!compressed.empty());

        auto decompressed = PluginStateUtils::decompress(compressed);
        REQUIRE(decompressed.size() == original.size());
        REQUIRE(decompressed == original);
    }

    SECTION("Compression actually reduces size for repetitive data") {
        // Repetitive data should compress well
        std::vector<uint8_t> repetitive(8192, 0xAB);
        auto compressed = PluginStateUtils::compress(repetitive);
        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < repetitive.size());

        auto decompressed = PluginStateUtils::decompress(compressed);
        REQUIRE(decompressed == repetitive);
    }
}

TEST_CASE("PluginStateUtils base64 encode/decode roundtrip", "[project][serialization][pluginstate]") {
    SECTION("Empty data") {
        std::vector<uint8_t> empty;
        auto encoded = PluginStateUtils::toBase64(empty);
        REQUIRE(encoded.isEmpty());

        auto decoded = PluginStateUtils::fromBase64(encoded);
        REQUIRE(decoded.empty());
    }

    SECTION("Binary data roundtrip") {
        std::vector<uint8_t> original = {0x00, 0x01, 0xFF, 0x80, 0x7F, 0xDE, 0xAD, 0xBE, 0xEF};
        auto encoded = PluginStateUtils::toBase64(original);
        REQUIRE(!encoded.isEmpty());

        auto decoded = PluginStateUtils::fromBase64(encoded);
        REQUIRE(decoded == original);
    }
}

TEST_CASE("PluginStateUtils compressAndEncode / decodeAndDecompress roundtrip",
          "[project][serialization][pluginstate]") {
    SECTION("Full pipeline roundtrip") {
        // Simulate a plugin state blob
        std::vector<uint8_t> original(2048);
        juce::Random rng(123);
        for (auto& byte : original) {
            byte = static_cast<uint8_t>(rng.nextInt(256));
        }

        auto encoded = PluginStateUtils::compressAndEncode(original);
        REQUIRE(!encoded.isEmpty());

        auto restored = PluginStateUtils::decodeAndDecompress(encoded);
        REQUIRE(restored.size() == original.size());
        REQUIRE(restored == original);
    }

    SECTION("Empty input produces empty output") {
        std::vector<uint8_t> empty;
        auto encoded = PluginStateUtils::compressAndEncode(empty);
        REQUIRE(encoded.isEmpty());

        auto restored = PluginStateUtils::decodeAndDecompress(encoded);
        REQUIRE(restored.empty());
    }
}

TEST_CASE("Device pluginStateData serialization roundtrip",
          "[project][serialization][pluginstate]") {
    ProjectTestFixture fixture;

    SECTION("Device with plugin state blob is saved and restored") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        // Create a track with a device that has plugin state data
        auto trackId = trackManager.createTrack("Synth Track", TrackType::MIDI);

        DeviceInfo device;
        device.id = 1;
        device.name = "My VST3 Synth";
        device.pluginId = "com.vendor.synth";
        device.manufacturer = "Vendor";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;
        device.bypassed = false;

        // Populate plugin state with test data (simulating an opaque binary blob)
        device.pluginStateData.resize(1024);
        juce::Random rng(999);
        for (auto& byte : device.pluginStateData) {
            byte = static_cast<uint8_t>(rng.nextInt(256));
        }
        auto originalState = device.pluginStateData;  // Copy for later comparison

        trackManager.addDeviceToTrack(trackId, device);

        // Save the project
        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear
        trackManager.clearAllTracks();
        REQUIRE(trackManager.getTracks().empty());

        // Load back
        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded == true);

        // Verify the plugin state was restored exactly
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]));

        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.name == "My VST3 Synth");
        REQUIRE(restoredDevice.pluginStateData.size() == originalState.size());
        REQUIRE(restoredDevice.pluginStateData == originalState);
    }

    SECTION("Device without plugin state has empty pluginStateData after load") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        auto trackId = trackManager.createTrack("FX Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "Delay";
        device.pluginId = "delay";
        device.format = PluginFormat::Internal;
        // No pluginStateData set — should remain empty after roundtrip

        trackManager.addDeviceToTrack(trackId, device);

        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        trackManager.clearAllTracks();

        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]));

        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.pluginStateData.empty());
    }

    SECTION("Large plugin state blob roundtrip") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        auto trackId = trackManager.createTrack("Big State Track", TrackType::MIDI);

        DeviceInfo device;
        device.id = 1;
        device.name = "Complex Plugin";
        device.pluginId = "com.vendor.complex";
        device.manufacturer = "Vendor";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;

        // A large state blob (256 KB — realistic for sample-based instruments)
        device.pluginStateData.resize(256 * 1024);
        juce::Random rng(777);
        for (auto& byte : device.pluginStateData) {
            byte = static_cast<uint8_t>(rng.nextInt(256));
        }
        auto originalState = device.pluginStateData;

        trackManager.addDeviceToTrack(trackId, device);

        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        trackManager.clearAllTracks();

        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(isDevice(tracks[0].chainElements[0]));

        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.pluginStateData.size() == originalState.size());
        REQUIRE(restoredDevice.pluginStateData == originalState);
    }
}
