#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/LegacyDeviceAliases.hpp"
#include "magda/daw/core/TrackInfo.hpp"

using namespace magda;
using Catch::Approx;

namespace {

ParameterInfo param(int index, float value) {
    ParameterInfo info;
    info.paramIndex = index;
    info.currentValue = value;
    return info;
}

DeviceInfo retiredDevice(const juce::String& pluginId, std::vector<ParameterInfo> params) {
    DeviceInfo device;
    device.name = "Old Device";
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    device.parameters = std::move(params);
    return device;
}

/// The migrated value for one slot, or nullopt when the slot was left to the
/// successor's own default.
std::optional<float> slot(const DeviceInfo& device, int paramIndex) {
    for (const auto& p : device.parameters)
        if (p.paramIndex == paramIndex)
            return p.currentValue;
    return std::nullopt;
}

}  // namespace

TEST_CASE("Retired Tracktion devices are recognised by every stored spelling",
          "[devices][legacy][aliases]") {
    for (const auto* id : {"4bandEq", "eq", "equaliser", "compressor", "delay", "chorus", "phaser",
                           "reverb", "pitchShifter", "pitchshift", "lowpass"}) {
        INFO("retired id: " << id);
        CHECK(legacy_devices::isRetiredDeviceId(id));
    }

    // Devices MAGDA still ships, and the successors themselves, are not.
    for (const auto* id : {"toneGenerator", "impulseresponse", "4osc", "volume", "magda_eq",
                           "magda_delay", "magda_filter", ""}) {
        INFO("live id: " << id);
        CHECK_FALSE(legacy_devices::isRetiredDeviceId(id));
    }
}

TEST_CASE("Migrating a retired device rewrites its identity", "[devices][legacy][aliases]") {
    auto device = retiredDevice("reverb", {param(0, 0.5f)});
    device.name = "Reverb";  // still the stock name
    device.pluginState = "<PLUGIN type=\"reverb\"/>";
    device.visibleParameters = {0, 1, 2};

    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(device.pluginId == "magda_reverb");
    CHECK(device.uniqueId == "magda_reverb");
    CHECK(device.name == "Reverb");
    // State and index lists described a plugin that no longer exists.
    CHECK(device.pluginState.isEmpty());
    CHECK(device.visibleParameters.empty());

    // Idempotent: a second pass finds nothing to do.
    CHECK_FALSE(legacy_devices::migrateRetiredDevice(device));
}

TEST_CASE("Migration keeps a user-renamed device's name", "[devices][legacy][aliases]") {
    auto device = retiredDevice("delay", {});
    device.name = "Slapback";

    REQUIRE(legacy_devices::migrateRetiredDevice(device));
    CHECK(device.name == "Slapback");
}

TEST_CASE("A device MAGDA still ships is left alone", "[devices][legacy][aliases]") {
    auto device = retiredDevice("toneGenerator", {param(2, 440.0f)});
    const auto before = device.pluginId;

    CHECK_FALSE(legacy_devices::migrateRetiredDevice(device));
    CHECK(device.pluginId == before);
    REQUIRE(device.parameters.size() == 1);
    CHECK(device.parameters[0].currentValue == 440.0f);
}

TEST_CASE("Compressor values convert out of Tracktion's units", "[devices][legacy][aliases]") {
    // threshold as a linear gain, 1/ratio, attack ms, release ms, output dB.
    auto device = retiredDevice("compressor", {param(0, 0.1f), param(1, 0.25f), param(2, 20.0f),
                                               param(3, 150.0f), param(4, 3.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(device.pluginId == "magda_compressor");
    CHECK(slot(device, 1).value() == Approx(-20.0f));  // 0.1 gain -> dB
    CHECK(slot(device, 2).value() == Approx(4.0f));    // 1/0.25 -> 4:1
    CHECK(slot(device, 3).value() == Approx(20.0f));
    CHECK(slot(device, 4).value() == Approx(150.0f));
    CHECK(slot(device, 8).value() == Approx(3.0f));
    CHECK(slot(device, 5).value() == Approx(0.0f));  // Tracktion is hard-knee
    CHECK(slot(device, 0).value() == Approx(0.0f));  // Clean engine
}

TEST_CASE("Compressor INF ratio maps to the maximum", "[devices][legacy][aliases]") {
    auto device = retiredDevice("compressor", {param(1, 0.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));
    CHECK(slot(device, 2).value() == Approx(50.0f));
}

TEST_CASE("Reverb folds wet and dry into one mix", "[devices][legacy][aliases]") {
    // Freeverb weights wet by 3 and dry by 2: equal weighted gain is a 50/50 mix.
    auto device = retiredDevice("reverb", {param(2, 1.0f / 3.0f), param(3, 0.5f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));
    CHECK(slot(device, 1).value() == Approx(0.5f));

    auto fullyWet = retiredDevice("reverb", {param(2, 1.0f), param(3, 0.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(fullyWet));
    CHECK(slot(fullyWet, 1).value() == Approx(1.0f));
}

TEST_CASE("Reverb room size and width scale to the successor's ranges",
          "[devices][legacy][aliases]") {
    auto device = retiredDevice("reverb", {param(0, 0.8f), param(1, 0.25f), param(4, 1.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(slot(device, 3).value() == Approx(80.0f));   // decay 0-100
    CHECK(slot(device, 4).value() == Approx(25.0f));   // damping 0-100
    CHECK(slot(device, 7).value() == Approx(100.0f));  // width 0-200, 100 = stereo
    // Freeze has no counterpart, so nothing claims a slot for it.
    CHECK(slot(device, 5) == std::nullopt);
}

TEST_CASE("Delay feedback converts from dB to a linear amount", "[devices][legacy][aliases]") {
    auto device = retiredDevice("delay", {param(0, -6.0f), param(1, 0.4f), param(2, 375.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(slot(device, 0).value() == Approx(375.0f));      // ms
    CHECK(slot(device, 3).value() == Approx(0.5011872f));  // -6 dB
    CHECK(slot(device, 4).value() == Approx(0.4f));        // mix
    CHECK(slot(device, 2).value() == Approx(0.0f));        // free-running, not synced

    // Tracktion treats the bottom of its feedback range as silence.
    auto silent = retiredDevice("delay", {param(0, -30.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(silent));
    CHECK(slot(silent, 3).value() == Approx(0.0f));
}

TEST_CASE("EQ bands become the first four compiled bands", "[devices][legacy][aliases]") {
    auto device = retiredDevice("4bandEq", {param(0, 120.0f), param(1, 3.0f), param(2, 0.7f),
                                            param(9, 8000.0f), param(10, -2.0f), param(11, 0.5f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(device.pluginId == "magda_eq");

    // Band 1 is the low shelf: enabled, type 1, then freq/gain/Q.
    CHECK(slot(device, 0).value() == Approx(1.0f));
    CHECK(slot(device, 1).value() == Approx(1.0f));
    CHECK(slot(device, 2).value() == Approx(120.0f));
    CHECK(slot(device, 3).value() == Approx(3.0f));
    CHECK(slot(device, 4).value() == Approx(0.7f));

    // Band 4 is the high shelf.
    CHECK(slot(device, 15).value() == Approx(1.0f));
    CHECK(slot(device, 16).value() == Approx(3.0f));
    CHECK(slot(device, 17).value() == Approx(8000.0f));
    CHECK(slot(device, 18).value() == Approx(-2.0f));

    // The two mids are bells, enabled even though the project stored no values
    // for them — Tracktion always ran all four filters.
    CHECK(slot(device, 6).value() == Approx(2.0f));
    CHECK(slot(device, 11).value() == Approx(2.0f));
    CHECK(slot(device, 7) == std::nullopt);
}

TEST_CASE("Lowpass carries its cutoff and low/high mode across", "[devices][legacy][aliases]") {
    auto lowpass = retiredDevice("lowpass", {param(0, 2000.0f), param(1, 0.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(lowpass));
    CHECK(lowpass.pluginId == "magda_filter");
    CHECK(slot(lowpass, 0).value() == Approx(2000.0f));
    CHECK(slot(lowpass, 4).value() == Approx(0.0f));  // LP

    auto highpass = retiredDevice("lowpass", {param(0, 300.0f), param(1, 1.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(highpass));
    CHECK(slot(highpass, 4).value() == Approx(2.0f));  // HP
}

TEST_CASE("Pitch Shift keeps its transposition and stays fully wet", "[devices][legacy][aliases]") {
    auto device = retiredDevice("pitchShifter", {param(0, -7.0f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(device));

    CHECK(device.pluginId == "magda_pitch");
    CHECK(slot(device, 1).value() == Approx(-7.0f));
    CHECK(slot(device, 4).value() == Approx(1.0f));
}

TEST_CASE("Chorus and Phaser anchor their defaults on the retired device's",
          "[devices][legacy][aliases]") {
    // Tracktion's chorus defaults: 3 ms depth, 1 Hz, 0.5 width, 0.5 mix.
    auto chorus =
        retiredDevice("chorus", {param(0, 3.0f), param(1, 1.0f), param(2, 0.5f), param(3, 0.5f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(chorus));
    CHECK(chorus.pluginId == "magda_chorus");
    CHECK(slot(chorus, 2).value() == Approx(1.0f));   // rate Hz
    CHECK(slot(chorus, 4).value() == Approx(0.15f));  // 3 of 20 ms sweep
    CHECK(slot(chorus, 6).value() == Approx(0.5f));   // mix
    CHECK(slot(chorus, 7).value() == Approx(0.5f));   // width
    CHECK(slot(chorus, 5).value() == Approx(0.0f));   // no feedback in Tracktion's

    // Tracktion's phaser defaults: 5 octaves of sweep, 0.4 Hz, 0.7 feedback.
    auto phaser = retiredDevice("phaser", {param(0, 5.0f), param(1, 0.4f), param(2, 0.7f)});
    REQUIRE(legacy_devices::migrateRetiredDevice(phaser));
    CHECK(phaser.pluginId == "magda_phaser");
    CHECK(slot(phaser, 0).value() == Approx(0.4f));
    CHECK(slot(phaser, 1).value() == Approx(1.0f));  // default sweep -> default depth
    CHECK(slot(phaser, 2).value() == Approx(0.7f));
    CHECK(slot(phaser, 3).value() == Approx(1.0f));  // four all-pass stages
}

TEST_CASE("Migrating a track drops the links that addressed a retired device",
          "[devices][legacy][aliases]") {
    TrackInfo track;
    track.id = 7;

    DeviceInfo retired = retiredDevice("phaser", {param(0, 5.0f)});
    retired.id = 3;
    DeviceInfo kept = retiredDevice("magda_delay", {param(0, 250.0f)});
    kept.id = 4;

    track.chain.fxChainElements.push_back(std::move(retired));
    track.chain.fxChainElements.push_back(std::move(kept));

    const auto retiredTarget = ChainNodePath::topLevelDevice(track.id, 3);
    const auto keptTarget = ChainNodePath::topLevelDevice(track.id, 4);

    track.macros[0].links.push_back({.target = {.devicePath = retiredTarget, .paramIndex = 0}});
    track.macros[0].links.push_back({.target = {.devicePath = keptTarget, .paramIndex = 0}});
    track.mods.push_back(ModInfo(0));
    track.mods[0].links.push_back({.target = {.devicePath = retiredTarget, .paramIndex = 2}});

    const auto migrated = legacy_devices::migrateRetiredDevicesInTrack(track);

    REQUIRE(migrated.size() == 1);
    CHECK(migrated[0] == retiredTarget);
    CHECK(getDevice(track.chain.fxChainElements[0]).pluginId == "magda_phaser");
    CHECK(getDevice(track.chain.fxChainElements[1]).pluginId == "magda_delay");

    // The link into the retired device is gone; the one next to it survives.
    REQUIRE(track.macros[0].links.size() == 1);
    CHECK(track.macros[0].links[0].target.devicePath == keptTarget);
    CHECK(track.mods[0].links.empty());
}

TEST_CASE("A project pass drops automation lanes on retired devices",
          "[devices][legacy][aliases]") {
    std::vector<TrackInfo> tracks(1);
    tracks[0].id = 2;

    DeviceInfo retired = retiredDevice("chorus", {param(0, 3.0f)});
    retired.id = 1;
    tracks[0].chain.fxChainElements.push_back(std::move(retired));

    AutomationLaneInfo retiredLane;
    retiredLane.id = 10;
    retiredLane.target.devicePath = ChainNodePath::topLevelDevice(2, 1);
    retiredLane.target.paramIndex = 0;

    AutomationLaneInfo trackVolumeLane;
    trackVolumeLane.id = 11;
    trackVolumeLane.target.kind = ControlTarget::Kind::TrackVolume;
    trackVolumeLane.target.devicePath = ChainNodePath::trackLevel(2);

    std::vector<AutomationLaneInfo> lanes{retiredLane, trackVolumeLane};

    AutomationClipInfo clip;
    clip.id = 20;
    clip.laneId = 10;
    std::vector<AutomationClipInfo> clips{clip};

    legacy_devices::migrateRetiredDevicesInProject(tracks, nullptr, lanes, clips);

    REQUIRE(lanes.size() == 1);
    CHECK(lanes[0].id == 11);
    CHECK(clips.empty());
}
