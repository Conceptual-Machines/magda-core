#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/generic_sound_design_agent.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace {

magda::ParameterInfo hosted(int slot, const juce::String& name, float current = 0.0f) {
    magda::ParameterInfo info(slot, name, "Hz", 100.0f, 1100.0f, 0.0f);
    info.valueConvention = magda::ParameterValueConvention::Normalized;
    info.teMinValue = 0.0f;
    info.teMaxValue = 1.0f;
    info.currentValue = current;
    // A configured display range remains normalized even without a live
    // formatter, as happens for persisted or mirrored hosted metadata.
    return info;
}

}  // namespace

TEST_CASE("Sound design sees selected unaddressed hosted parameters in plugin positions",
          "[sound-design][parameters][2655][2623]") {
    std::vector<magda::ParameterInfo> live;
    live.push_back(hosted(2, "Cutoff", 0.25f));
    live.push_back(hosted(7, "Cutoff", 0.75f));

    const auto snapshot = magda::sound_design_detail::snapshotParameters(std::move(live), {7});
    REQUIRE(snapshot.size() == 1);
    CHECK(snapshot[0].described.paramIndex == 7);
    CHECK(snapshot[0].promptName == "Cutoff");
    CHECK(snapshot[0].currentRealValue == Catch::Approx(850.0f));

    int skipped = 0;
    const std::vector<std::pair<juce::String, juce::var>> request{{"Cutoff", 600.0}};
    const auto writes =
        magda::sound_design_detail::resolveParameterWrites(snapshot, request, skipped);
    REQUIRE(writes.size() == 1);
    CHECK(skipped == 0);
    CHECK(writes[0].first.paramIndex == 7);
    CHECK(writes[0].second.value == Catch::Approx(0.5f));

    CHECK(magda::ParameterUtils::modelToNormalizedValue(writes[0].second, writes[0].first).value ==
          Catch::Approx(0.5f));
}

TEST_CASE("Sound design preserves sparse slots, configured ranges, and duplicate names",
          "[sound-design][parameters][2655][2623]") {
    std::vector<magda::ParameterInfo> live;
    live.push_back(hosted(3, "Rate", 0.1f));
    live.push_back(hosted(19, "Rate", 0.9f));

    const auto snapshot = magda::sound_design_detail::snapshotParameters(std::move(live), {});
    REQUIRE(snapshot.size() == 2);
    CHECK(snapshot[0].promptName == "Rate [#3]");
    CHECK(snapshot[1].promptName == "Rate [#19]");

    int skipped = 0;
    const std::vector<std::pair<juce::String, juce::var>> request{{"Rate [#19]", 350.0}};
    const auto writes =
        magda::sound_design_detail::resolveParameterWrites(snapshot, request, skipped);
    REQUIRE(writes.size() == 1);
    CHECK(writes[0].first.paramIndex == 19);
    CHECK(writes[0].second.value == Catch::Approx(0.25f));

    auto& tracks = magda::TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Sound design", magda::TrackType::Media);
    magda::DeviceInfo device;
    device.id = 71;
    device.name = "Configured host";
    device.format = magda::PluginFormat::VST3;
    device.parameters.push_back(hosted(19, "Rate", 0.9f));
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    const auto path = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

    tracks.setDeviceParameterValue(path, writes[0].first, writes[0].second);
    const auto* updated = tracks.getDeviceInChainByPath(path);
    REQUIRE(updated != nullptr);
    REQUIRE(updated->findParameterByIndex(19) != nullptr);
    CHECK(updated->findParameterByIndex(19)->currentValue == Catch::Approx(0.25f));
    tracks.clearAllTracks();
}

TEST_CASE("Sound design keeps internal parameter model values in real units",
          "[sound-design][parameters][2655]") {
    magda::ParameterInfo internal(11, "Decay", "ms", 10.0f, 1010.0f, 510.0f);
    internal.teMinValue = 0.0f;
    internal.teMaxValue = 1.0f;
    internal.currentValue = 260.0f;

    const auto snapshot = magda::sound_design_detail::snapshotParameters({internal}, {});
    REQUIRE(snapshot.size() == 1);
    CHECK(snapshot[0].currentRealValue == Catch::Approx(260.0f));

    int skipped = 0;
    const std::vector<std::pair<juce::String, juce::var>> request{{"Decay", 760.0}};
    const auto writes =
        magda::sound_design_detail::resolveParameterWrites(snapshot, request, skipped);
    REQUIRE(writes.size() == 1);
    CHECK(writes[0].second.value == Catch::Approx(760.0f));
    CHECK(magda::ParameterUtils::modelToNormalizedValue(writes[0].second, writes[0].first).value ==
          Catch::Approx(0.75f));
}

TEST_CASE("Sound design retains legacy internal vector slots", "[sound-design][parameters][2655]") {
    magda::ParameterInfo first(-1, "Attack", "ms", 0.0f, 1000.0f, 10.0f);
    magda::ParameterInfo second(-1, "Release", "ms", 0.0f, 1000.0f, 100.0f);

    const auto snapshot = magda::sound_design_detail::snapshotParameters({first, second}, {1});
    REQUIRE(snapshot.size() == 1);
    CHECK(snapshot[0].described.paramIndex == 1);
    CHECK(snapshot[0].promptName == "Release");
}
