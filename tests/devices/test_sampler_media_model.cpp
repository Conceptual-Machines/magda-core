#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/sampling/SamplerMedia.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

juce::String sourceOf(const ChainNodePath& path) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    const auto doc = device != nullptr ? device_state::decode(device->pluginState) : std::nullopt;
    return doc ? doc->root.props["source"].toString() : juce::String();
}

}  // namespace

TEST_CASE("The model's samplers are walked and repointed where they sit",
          "[sampler][missing-media][2784]") {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("magda-sampler-media-model");
    dir.deleteRecursively();
    REQUIRE(dir.createDirectory());
    const auto kick = dir.getChildFile("kick.wav");
    const auto snare = dir.getChildFile("snare.wav");
    REQUIRE(kick.replaceWithText("kick"));
    REQUIRE(snare.replaceWithText("snare"));

    const auto samplerTrack = tracks.createTrack("Sampler");
    const auto samplerId =
        tracks.addDeviceToTrack(samplerTrack, padSamplerDevice(kick.getFullPathName(), 48));
    const auto samplerPath = ChainNodePath::topLevelDevice(samplerTrack, samplerId);

    DeviceInfo grid;
    grid.name = "Drum Grid";
    grid.pluginId = "drumgrid";
    grid.isInstrument = true;
    grid.format = PluginFormat::Internal;
    grid.pads.reset(std::make_unique<RackInfo>());
    const auto gridTrack = tracks.createTrack("Drums");
    const auto gridId = tracks.addDeviceToTrack(gridTrack, grid);
    const auto gridPath = ChainNodePath::topLevelDevice(gridTrack, gridId);
    tracks.ensurePad(gridPath, 0);
    tracks.setPadDevice(gridPath, 0, padSamplerDevice(snare.getFullPathName(), 36));

    auto references = modelSamplerMedia();
    std::vector<juce::String> found;
    for (const auto& reference : references)
        found.push_back(reference.source.getFullPathName());
    std::ranges::sort(found);
    std::vector<juce::String> expected{kick.getFullPathName(), snare.getFullPathName()};
    std::ranges::sort(expected);
    CHECK(found == expected);

    const auto moved = dir.getChildFile("moved").getChildFile("kick.wav");
    const auto kickReference = std::ranges::find_if(
        references, [&kick](const auto& reference) { return reference.source == kick; });
    REQUIRE(kickReference != references.end());
    kickReference->replace(moved);
    CHECK(sourceOf(samplerPath) == moved.getFullPathName());
    const auto* device = tracks.getDeviceInChainByPath(samplerPath);
    REQUIRE(device != nullptr);
    CHECK(static_cast<int>(device_state::decode(device->pluginState)->root.props["rootNote"]) ==
          48);

    tracks.clearAllTracks();
    dir.deleteRecursively();
}
