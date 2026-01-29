#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/AutomationManager.hpp"
#include "../magda/daw/core/ClipManager.hpp"
#include "../magda/daw/core/TrackManager.hpp"
#include "../magda/daw/project/ProjectManager.hpp"
#include "../magda/daw/project/ProjectSerializer.hpp"

using namespace magda;

// Test fixture to ensure clean state between tests
struct ProjectTestFixture {
    ProjectTestFixture() {
        // Clear all singleton state before each test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        // Note: AutomationManager doesn't have clearAllLanes() yet
    }

    ~ProjectTestFixture() {
        // Clean up after test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        // Note: AutomationManager doesn't have clearAllLanes() yet
    }

    // Helper to create unique temp file
    juce::File createTempFile(const juce::String& basename) {
        return juce::File::createTempFile(basename);
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
        tempFile.deleteFile();
    }

    SECTION("Project info serialization roundtrip") {
        ProjectInfo info;
        info.name = "Test Project";
        info.tempo = 128.0;
        info.timeSignatureNumerator = 3;
        info.timeSignatureDenominator = 4;
        info.loopEnabled = true;
        info.loopStart = 4.0;
        info.loopEnd = 16.0;

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
        REQUIRE(loaded.loopStart == info.loopStart);
        REQUIRE(loaded.loopEnd == info.loopEnd);
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
        tempFile.deleteFile();
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

        tempFile.deleteFile();
    }

    SECTION("File is not empty") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempFile(".mgd");

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(tempFile.getSize() > 0);

        tempFile.deleteFile();
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
        tempFile.deleteFile();
        trackManager.clearAllTracks();
    }

    SECTION("getCurrentProjectFile returns correct file") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempFile(".mgd");
        projectManager.saveProjectAs(tempFile);

        auto currentFile = projectManager.getCurrentProjectFile();
        REQUIRE(currentFile.getFullPathName() == tempFile.getFullPathName());

        tempFile.deleteFile();
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
