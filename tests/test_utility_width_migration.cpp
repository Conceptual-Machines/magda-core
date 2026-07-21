#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

using namespace magda;

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root.createDirectory();
    return root;
}

struct MigrationFixture {
    std::vector<juce::File> tempFiles;
    std::vector<juce::File> tempDirs;

    MigrationFixture() {
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~MigrationFixture() {
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
        auto file = testTempRoot().getNonexistentChildFile("width_migration", suffix);
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

ParameterInfo makeWidthParam(float minValue, float maxValue, float defaultValue,
                             float currentValue) {
    ParameterInfo param;
    param.paramIndex = 2;  // MagdaUtilityCompiledPlugin::kWidthSlot
    param.name = "Width";
    param.minValue = minValue;
    param.maxValue = maxValue;
    param.defaultValue = defaultValue;
    param.currentValue = currentValue;
    param.scale = ParameterScale::Linear;
    return param;
}

}  // namespace

TEST_CASE("Old-scale Utility Width migrates to percent on load",
          "[project][serialization][migration]") {
    MigrationFixture fixture;

    auto& tm = TrackManager::getInstance();
    auto& pm = ProjectManager::getInstance();

    // A pre-0.16 project stored Width as a 0-2 multiplier. Recreate that model
    // shape and save it; the file then carries the old-scale values verbatim.
    auto trackId = tm.createTrack("Utility Track");
    DeviceInfo utility;
    utility.name = "Utility";
    utility.pluginId = "magda_utility";
    utility.format = PluginFormat::Internal;
    utility.parameters.push_back(makeWidthParam(0.0f, 2.0f, 1.0f, 1.5f));
    auto deviceId = tm.addDeviceToTrack(trackId, utility);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = MigrationFixture::wrappedPath(tempFile);
    REQUIRE(pm.saveProjectAs(tempFile));

    tm.clearAllTracks();
    REQUIRE(pm.loadProject(actualFile));

    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    REQUIRE(isDevice(track->chain.fxChainElements[0]));

    const auto& device = getDevice(track->chain.fxChainElements[0]);
    REQUIRE(device.parameters.size() == 1);
    const auto& width = device.parameters[0];
    CHECK(width.currentValue == 150.0f);
    CHECK(width.minValue == 0.0f);
    CHECK(width.maxValue == 200.0f);
    CHECK(width.defaultValue == 100.0f);
    CHECK(width.unit == "%");
}

TEST_CASE("Percent-scale Utility Width passes through load unchanged",
          "[project][serialization][migration]") {
    MigrationFixture fixture;

    auto& tm = TrackManager::getInstance();
    auto& pm = ProjectManager::getInstance();

    auto trackId = tm.createTrack("Utility Track");
    DeviceInfo utility;
    utility.name = "Utility";
    utility.pluginId = "magda_utility";
    utility.format = PluginFormat::Internal;
    utility.parameters.push_back(makeWidthParam(0.0f, 200.0f, 100.0f, 150.0f));
    auto deviceId = tm.addDeviceToTrack(trackId, utility);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = MigrationFixture::wrappedPath(tempFile);
    REQUIRE(pm.saveProjectAs(tempFile));

    tm.clearAllTracks();
    REQUIRE(pm.loadProject(actualFile));

    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);

    const auto& device = getDevice(track->chain.fxChainElements[0]);
    REQUIRE(device.parameters.size() == 1);
    CHECK(device.parameters[0].currentValue == 150.0f);
    CHECK(device.parameters[0].maxValue == 200.0f);
}
