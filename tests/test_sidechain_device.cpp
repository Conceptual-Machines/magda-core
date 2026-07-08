#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "../magda/daw/audio/plugins/SidechainPlugin.hpp"
#include "../magda/daw/core/InternalDeviceKind.hpp"
#include "../magda/daw/core/TrackManager.hpp"
#include "../magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// ============================================================================
// Sidechain device (issue #1591) — registration + creation-time mod seeding
// ============================================================================

TEST_CASE("Sidechain device - classification and registry", "[sidechain][device]") {
    SECTION("pluginId classifies to the Sidechain kind") {
        REQUIRE(classifyInternalDevice("sidechain") == InternalDeviceKind::Sidechain);
        REQUIRE(classifyInternalDevice("SIDECHAIN") == InternalDeviceKind::Sidechain);
    }

    SECTION("does not collide with the monitor infrastructure kinds") {
        REQUIRE(classifyInternalDevice("midisidechainmonitor") ==
                InternalDeviceKind::SidechainMonitor);
        REQUIRE(classifyInternalDevice("audiosidechainmonitor") ==
                InternalDeviceKind::AudioSidechainMonitor);
    }

    SECTION("metadata present") {
        const auto* metadata = getInternalDeviceMetadata(InternalDeviceKind::Sidechain);
        REQUIRE(metadata != nullptr);
        REQUIRE(juce::String(metadata->displayName) == "Sidechain");
        REQUIRE(juce::String(metadata->category) == "Dynamics");
    }

    SECTION("registry spec is browsable, effect, with a processor factory") {
        const daw::audio::InternalPluginSpec* spec = nullptr;
        for (const auto* s : daw::audio::getAllInternalPluginSpecs()) {
            if (juce::String(s->pluginId).equalsIgnoreCase("sidechain")) {
                spec = s;
                break;
            }
        }
        REQUIRE(spec != nullptr);
        REQUIRE(spec->kind == InternalDeviceKind::Sidechain);
        REQUIRE(spec->showInBrowser);
        REQUIRE_FALSE(spec->isInstrument);
        REQUIRE(spec->createProcessor != nullptr);
    }

    SECTION("not an analysis device (keeps macros and mods)") {
        REQUIRE_FALSE(isAnalysisDevice("sidechain"));
        REQUIRE_FALSE(isMidiGeneratorDevice("sidechain"));
    }
}

TEST_CASE("Sidechain device - creation-time mod seeding", "[sidechain][device]") {
    const auto path = ChainNodePath::topLevelDevice(3, 42);

    DeviceInfo device;
    device.id = 42;
    device.pluginId = "sidechain";
    device.format = PluginFormat::Internal;

    SECTION("seeds the duck curve modulator with a full-depth gain link") {
        TrackManager::seedSidechainModIfMissing(device, path);

        REQUIRE(device.mods.size() == 1);
        const auto& mod = device.mods[0];
        REQUIRE(mod.type == ModType::LFO);
        REQUIRE(mod.enabled);
        REQUIRE(mod.waveform == LFOWaveform::Custom);
        REQUIRE(mod.curvePreset == CurvePreset::Custom);
        REQUIRE(mod.triggerMode == LFOTriggerMode::MIDI);
        REQUIRE(mod.tempoSync);
        REQUIRE(mod.syncDivision == SyncDivision::Quarter);
        REQUIRE(mod.oneShot);

        // The drawn curve is the audible gain envelope: ducked at note-on,
        // recovering to full level. invertOutput makes the applied output the
        // duck amount (1 - curve), so the one-shot hold at end value 1 and an
        // inactive modifier (output 0) both mean unity gain.
        REQUIRE(mod.invertOutput);
        REQUIRE(mod.curvePoints.size() == 2);
        REQUIRE(mod.curvePoints.front().phase == Catch::Approx(0.0f));
        REQUIRE(mod.curvePoints.front().value == Catch::Approx(0.0f));
        REQUIRE(mod.curvePoints.back().phase == Catch::Approx(1.0f));
        REQUIRE(mod.curvePoints.back().value == Catch::Approx(1.0f));

        REQUIRE(mod.links.size() == 1);
        const auto& link = mod.links[0];
        REQUIRE(link.enabled);
        REQUIRE_FALSE(link.bipolar);
        REQUIRE(link.amount == Catch::Approx(-1.0f));
        const auto expected =
            ControlTarget::pluginParam(path, daw::audio::SidechainPlugin::kGainParamIndex);
        REQUIRE(link.target == expected);
    }

    SECTION("does not reseed when mods already exist (duplication keeps the curve)") {
        TrackManager::seedSidechainModIfMissing(device, path);
        device.mods[0].curvePoints[0].value = 0.5f;

        TrackManager::seedSidechainModIfMissing(device, path);
        REQUIRE(device.mods.size() == 1);
        REQUIRE(device.mods[0].curvePoints[0].value == Catch::Approx(0.5f));
    }

    SECTION("ignores other devices") {
        DeviceInfo other;
        other.id = 7;
        other.pluginId = "reverb";
        TrackManager::seedSidechainModIfMissing(other, ChainNodePath::topLevelDevice(3, 7));
        REQUIRE(other.mods.empty());
    }
}

TEST_CASE("Sidechain device - state survives a serialization round-trip",
          "[sidechain][device][serialization]") {
    const auto path = ChainNodePath::topLevelDevice(3, 42);

    DeviceInfo device;
    device.id = 42;
    device.pluginId = "sidechain";
    device.format = PluginFormat::Internal;
    TrackManager::seedSidechainModIfMissing(device, path);
    device.sidechain.type = SidechainConfig::Type::MIDI;
    device.sidechain.sourceTrackId = 7;

    auto json = ProjectSerializer::serializeDeviceInfo(device);
    DeviceInfo loaded;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, loaded));

    // The MIDI source routing must come back so the header sidechain button
    // re-engages and the monitor is re-inserted on the source track.
    REQUIRE(loaded.sidechain.isActive());
    REQUIRE(loaded.sidechain.type == SidechainConfig::Type::MIDI);
    REQUIRE(loaded.sidechain.sourceTrackId == 7);

    REQUIRE(loaded.mods.size() == 1);
    const auto& mod = loaded.mods[0];
    REQUIRE(mod.invertOutput);
    REQUIRE(mod.oneShot);
    REQUIRE(mod.triggerMode == LFOTriggerMode::MIDI);
    REQUIRE(mod.tempoSync);
    REQUIRE(mod.curvePoints.size() == 2);
    REQUIRE(mod.links.size() == 1);
    REQUIRE(mod.links[0].amount == Catch::Approx(-1.0f));
    REQUIRE(mod.links[0].target ==
            ControlTarget::pluginParam(path, daw::audio::SidechainPlugin::kGainParamIndex));
}
