#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/PresetManager.hpp"

namespace {

using namespace magda;

struct PresetDirectoryScope {
    PresetDirectoryScope()
        : previous(Config::getInstance().getPresetsDir()),
          directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("magda-track-presets", {})) {
        REQUIRE(directory.createDirectory());
        Config::getInstance().setPresetsDir(directory.getFullPathName().toStdString());
        paths::resolve();
    }

    ~PresetDirectoryScope() {
        Config::getInstance().setPresetsDir(previous);
        paths::resolve();
        directory.deleteRecursively();
    }

    std::string previous;
    juce::File directory;
};

DeviceInfo device(DeviceId id, const char* name, const char* state) {
    DeviceInfo result;
    result.id = id;
    result.name = name;
    result.pluginId = juce::String(name).toLowerCase();
    result.format = PluginFormat::VST3;
    result.pluginState = state;
    return result;
}

}  // namespace

TEST_CASE("Track-chain presets expose stable path-free metadata and retain full track state",
          "[presets][track][remote]") {
    PresetDirectoryScope scope;
    auto& presets = PresetManager::getInstance();

    TrackInfo source;
    source.id = 87;
    source.name = "Source Track";
    source.volume = 0.42f;
    source.pan = -0.25f;
    source.audioOutputDevice = "master";
    source.chain.enabled = false;
    source.chain.postFxPostFader = false;
    source.chain.fxChainElements.push_back(makeDeviceElement(device(10, "Kontakt", "opaque-fx")));
    source.chain.postFxChainElements.push_back({device(20, "Limiter", "opaque-post")});

    REQUIRE(presets.saveChainPreset(source, "Strings/Emotional Violin"));
    auto listed = presets.getTrackPresetMetadata();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id.startsWith("track-preset:"));
    CHECK(listed[0].name == "Emotional Violin");
    CHECK(listed[0].category == "Strings");
    CHECK_FALSE(listed[0].id.contains(scope.directory.getFullPathName()));
    const auto stableId = listed[0].id;

    PresetManager::TrackPreset loaded;
    REQUIRE(presets.loadTrackPresetById(stableId, loaded));
    REQUIRE(loaded.hasTrackSettings);
    CHECK(loaded.track.name == "Source Track");
    CHECK(loaded.track.volume == source.volume);
    CHECK(loaded.track.pan == source.pan);
    CHECK_FALSE(loaded.track.chain.enabled);
    CHECK_FALSE(loaded.track.chain.postFxPostFader);
    REQUIRE(loaded.track.chain.fxChainElements.size() == 1);
    REQUIRE(loaded.track.chain.postFxChainElements.size() == 1);
    CHECK(getDevice(loaded.track.chain.fxChainElements.front()).pluginState == "opaque-fx");
    CHECK(loaded.track.chain.postFxChainElements.front().device.pluginState == "opaque-post");

    // Overwrite and rename both keep the opaque identity.
    source.volume = 0.75f;
    REQUIRE(presets.saveChainPreset(source, "Strings/Emotional Violin"));
    REQUIRE(presets.renameChainPreset("Strings/Emotional Violin", "Orchestral/Violin"));
    listed = presets.getTrackPresetMetadata();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == stableId);
    CHECK(listed[0].name == "Violin");
    CHECK(listed[0].category == "Orchestral");
}

TEST_CASE("Legacy chain-only presets are addressable and promoted for track creation",
          "[presets][track][legacy]") {
    PresetDirectoryScope scope;
    auto& presets = PresetManager::getInstance();

    std::vector<ChainElement> elements;
    elements.push_back(makeDeviceElement(device(3, "Legacy Synth", "opaque-legacy")));
    REQUIRE(presets.saveChainPreset(elements, "Legacy/Synth"));

    const auto listed = presets.getTrackPresetMetadata();
    REQUIRE(listed.size() == 1);
    PresetManager::TrackPreset loaded;
    REQUIRE(presets.loadTrackPresetById(listed[0].id, loaded));
    CHECK_FALSE(loaded.hasTrackSettings);
    CHECK(loaded.track.name == "Synth");
    CHECK(loaded.track.audioOutputDevice == "master");
    CHECK(loaded.track.midiInputDevice == "all");
    REQUIRE(loaded.track.chain.fxChainElements.size() == 1);
    CHECK(getDevice(loaded.track.chain.fxChainElements.front()).pluginState == "opaque-legacy");
}

TEST_CASE("Device presets expose stable path-free metadata", "[presets][device][remote]") {
    PresetDirectoryScope scope;
    auto& presets = PresetManager::getInstance();
    auto source = device(3, "Serum", "opaque-state");

    REQUIRE(presets.saveDevicePreset(source, "Bass/Reese 808"));
    auto listed = presets.getDevicePresetMetadata(source.name);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id.startsWith("device-preset:"));
    CHECK(listed[0].name == "Reese 808");
    CHECK(listed[0].category == "Bass");
    CHECK_FALSE(listed[0].id.contains(scope.directory.getFullPathName()));
    const auto stableId = listed[0].id;

    REQUIRE(presets.renameDevicePreset(source.name, "Bass/Reese 808", "Factory/Reese"));
    listed = presets.getDevicePresetMetadata(source.name);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == stableId);
    CHECK(listed[0].name == "Reese");
    CHECK(listed[0].category == "Factory");
}
