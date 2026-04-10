#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// ============================================================================
// Test Fixture
// ============================================================================

struct MasterSerializationFixture {
    std::vector<juce::File> tempFiles;
    std::vector<juce::File> tempDirs;

    MasterSerializationFixture() {
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~MasterSerializationFixture() {
        for (auto& dir : tempDirs) {
            if (dir.isDirectory())
                dir.deleteRecursively();
        }
        for (auto& file : tempFiles) {
            if (file.existsAsFile())
                file.deleteFile();
        }
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    juce::File createTempProjectFile(const juce::String& suffix) {
        auto file = juce::File::createTempFile(suffix);
        tempFiles.push_back(file);
        auto wrapperDir =
            file.getParentDirectory().getChildFile(file.getFileNameWithoutExtension());
        tempDirs.push_back(wrapperDir);
        return file;
    }

    static juce::File wrappedPath(const juce::File& file) {
        auto projectName = file.getFileNameWithoutExtension();
        auto parentDir = file.getParentDirectory();
        if (parentDir.getFileName() != projectName) {
            auto wrapperDir = parentDir.getChildFile(projectName);
            return wrapperDir.getChildFile(file.getFileName());
        }
        return file;
    }
};

// ============================================================================
// Master Track Chain Serialization (#959)
// ============================================================================

TEST_CASE("Master track chain elements survive save/load", "[project][serialization][master]") {
    MasterSerializationFixture fixture;

    auto& tm = TrackManager::getInstance();
    auto& pm = ProjectManager::getInstance();

    // Add a device to the master track
    DeviceInfo eqDevice;
    eqDevice.name = "EQ";
    eqDevice.pluginId = "eq";
    eqDevice.format = PluginFormat::Internal;
    auto eqId = tm.addDeviceToTrack(MASTER_TRACK_ID, eqDevice);
    REQUIRE(eqId != INVALID_DEVICE_ID);

    // Verify it's on the master track
    auto* master = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(master != nullptr);
    REQUIRE(master->chainElements.size() == 1);

    // Save
    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = MasterSerializationFixture::wrappedPath(tempFile);
    bool saved = pm.saveProjectAs(tempFile);
    INFO("saveProjectAs error: " << pm.getLastError());
    REQUIRE(saved);

    // Clear state
    tm.clearAllTracks();
    master = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(master->chainElements.empty());

    // Load back
    bool loaded = pm.loadProject(actualFile);
    REQUIRE(loaded);

    // Verify master chain was restored
    master = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(master != nullptr);
    REQUIRE(master->chainElements.size() == 1);

    auto& element = master->chainElements[0];
    REQUIRE(isDevice(element));
    REQUIRE(getDevice(element).pluginId == "eq");
}

TEST_CASE("Master track chain with multiple devices roundtrips",
          "[project][serialization][master]") {
    MasterSerializationFixture fixture;

    auto& tm = TrackManager::getInstance();
    auto& pm = ProjectManager::getInstance();

    // Add multiple devices to master
    DeviceInfo eq;
    eq.name = "EQ";
    eq.pluginId = "eq";
    eq.format = PluginFormat::Internal;
    tm.addDeviceToTrack(MASTER_TRACK_ID, eq);

    DeviceInfo comp;
    comp.name = "Compressor";
    comp.pluginId = "compressor";
    comp.format = PluginFormat::Internal;
    tm.addDeviceToTrack(MASTER_TRACK_ID, comp);

    auto* master = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(master->chainElements.size() == 2);

    // Save and reload
    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = MasterSerializationFixture::wrappedPath(tempFile);
    REQUIRE(pm.saveProjectAs(tempFile));
    tm.clearAllTracks();
    REQUIRE(pm.loadProject(actualFile));

    master = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(master->chainElements.size() == 2);
    REQUIRE(getDevice(master->chainElements[0]).pluginId == "eq");
    REQUIRE(getDevice(master->chainElements[1]).pluginId == "compressor");
}
