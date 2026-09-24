#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/core/DrumGridPads.hpp"
#include "../../magda/daw/core/RackInfo.hpp"
#include "../../magda/daw/core/TrackCommands.hpp"
#include "../../magda/daw/core/TrackManager.hpp"
#include "../../magda/daw/core/UndoManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture Helper
// ============================================================================

class RackAudioTestFixture {
  public:
    RackAudioTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~RackAudioTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }
};

TEST_CASE("MAGDA device presets retarget device-local macro and mod links",
          "[rack_audio][device_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Preset Target");

    DeviceInfo liveTemplate;
    liveTemplate.name = "4OSC Synth";
    liveTemplate.format = PluginFormat::Internal;
    liveTemplate.pluginId = "4osc";

    auto liveDeviceId = fixture.tm().addDeviceToTrack(trackId, liveTemplate);
    auto livePath = ChainNodePath::topLevelDevice(trackId, liveDeviceId);

    DeviceInfo preset = liveTemplate;
    preset.id = 99;
    preset.parameters.resize(64);
    preset.pluginState = "<PLUGIN><MODIFIERASSIGNMENTS><LFO source=\"1\" paramID=\"filterFreq\" "
                         "value=\"0.5\"/></MODIFIERASSIGNMENTS></PLUGIN>";

    MacroInfo macro(0);
    macro.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::topLevelDevice(123, preset.id), 7), 0.25f,
         false});
    preset.macros = {macro};

    ModInfo mod(0);
    mod.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::topLevelDevice(123, preset.id), 8), 0.5f, true});
    preset.mods = {mod};

    REQUIRE(fixture.tm().applyDevicePreset(livePath, preset));

    auto* live = fixture.tm().getDeviceInChainByPath(livePath);
    REQUIRE(live != nullptr);
    REQUIRE(live->macros.size() == 1);
    REQUIRE(live->mods.size() == 1);
    REQUIRE(live->macros[0].links.size() == 1);
    REQUIRE(live->mods[0].links.size() == 1);

    CHECK(live->macros[0].links[0].target.devicePath == livePath);
    CHECK(live->mods[0].links[0].target.devicePath == livePath);
    CHECK(live->macros[0].links[0].target.paramIndex == 7);
    CHECK(live->mods[0].links[0].target.paramIndex == 8);
    CHECK(!live->pluginState.contains("MODIFIERASSIGNMENTS"));
}

TEST_CASE("MAGDA rack presets retarget internal macro and mod links",
          "[rack_audio][rack_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Rack Preset Target");
    auto liveRackId = fixture.tm().addRackToTrack(trackId, "Live Rack");
    auto liveRackPath = ChainNodePath::rack(trackId, liveRackId);

    RackInfo presetRack;
    presetRack.id = 90;
    presetRack.name = "Preset Rack";

    ChainInfo presetChain;
    presetChain.id = 91;

    DeviceInfo presetDevice;
    presetDevice.id = 92;
    presetDevice.name = "Delay";
    presetDevice.format = PluginFormat::Internal;
    presetDevice.pluginId = "delay";
    presetDevice.pluginState = "<PLUGIN><MODIFIERASSIGNMENTS><LFO source=\"1\" paramID=\"mix\" "
                               "value=\"0.5\"/></MODIFIERASSIGNMENTS></PLUGIN>";

    auto oldTarget =
        ChainNodePath::chainDevice(123, presetRack.id, presetChain.id, presetDevice.id);

    MacroInfo rackMacro(0);
    rackMacro.links.push_back({ControlTarget::pluginParam(oldTarget, 3), 0.25f, false});
    presetRack.macros = {rackMacro};

    ModInfo deviceMod(0);
    deviceMod.links.push_back({ControlTarget::pluginParam(oldTarget, 4), 0.5f, true});
    presetDevice.mods = {deviceMod};

    presetChain.elements.push_back(makeDeviceElement(presetDevice));
    presetRack.chains.push_back(std::move(presetChain));

    REQUIRE(fixture.tm().applyRackPreset(liveRackPath, presetRack));

    auto* liveRack = fixture.tm().getRackByPath(liveRackPath);
    REQUIRE(liveRack != nullptr);
    REQUIRE(liveRack->chains.size() == 1);
    REQUIRE(liveRack->chains[0].elements.size() == 1);
    REQUIRE(isDevice(liveRack->chains[0].elements[0]));

    auto& liveDevice = getDevice(liveRack->chains[0].elements[0]);
    auto expectedTarget =
        ChainNodePath::chainDevice(trackId, liveRackId, liveRack->chains[0].id, liveDevice.id);

    REQUIRE(liveRack->macros.size() == 1);
    REQUIRE(liveRack->macros[0].links.size() == 1);
    REQUIRE(liveDevice.mods.size() == 1);
    REQUIRE(liveDevice.mods[0].links.size() == 1);

    CHECK(liveRack->macros[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.mods[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveRack->macros[0].links[0].target.paramIndex == 3);
    CHECK(liveDevice.mods[0].links[0].target.paramIndex == 4);
    CHECK(!liveDevice.pluginState.contains("MODIFIERASSIGNMENTS"));
}

TEST_CASE("MAGDA track presets retarget top-level macro and mod links",
          "[rack_audio][track_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Track Preset Target");

    DeviceInfo presetDevice;
    presetDevice.id = 200;
    presetDevice.name = "Delay";
    presetDevice.format = PluginFormat::Internal;
    presetDevice.pluginId = "delay";

    auto oldTarget = ChainNodePath::topLevelDevice(123, presetDevice.id);

    MacroInfo macro(0);
    macro.links.push_back({ControlTarget::pluginParam(oldTarget, 5), 0.25f, false});
    presetDevice.macros = {macro};

    ModInfo mod(0);
    mod.links.push_back({ControlTarget::pluginParam(oldTarget, 6), 0.5f, true});
    presetDevice.mods = {mod};

    std::vector<ChainElement> presetElements;
    presetElements.push_back(makeDeviceElement(presetDevice));

    REQUIRE(fixture.tm().applyChainPreset(trackId, std::move(presetElements)));

    auto* track = fixture.tm().getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    REQUIRE(isDevice(track->chain.fxChainElements[0]));

    auto& liveDevice = getDevice(track->chain.fxChainElements[0]);
    auto expectedTarget = ChainNodePath::topLevelDevice(trackId, liveDevice.id);

    REQUIRE(liveDevice.macros.size() == 1);
    REQUIRE(liveDevice.mods.size() == 1);
    REQUIRE(liveDevice.macros[0].links.size() == 1);
    REQUIRE(liveDevice.mods[0].links.size() == 1);

    CHECK(liveDevice.macros[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.mods[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.macros[0].links[0].target.paramIndex == 5);
    CHECK(liveDevice.mods[0].links[0].target.paramIndex == 6);
}

TEST_CASE("Creating a track from a preset restores every chain section and rekeys links",
          "[rack_audio][track_presets][create]") {
    RackAudioTestFixture fixture;

    TrackInfo preset;
    preset.id = 700;
    preset.name = "Saved Name";
    preset.volume = 0.37f;
    preset.manualVolume = 0.37f;
    preset.pan = -0.4f;
    preset.manualPan = -0.4f;
    preset.muted = true;
    preset.midiInputDevice = "all";
    preset.chain.enabled = false;
    preset.chain.postFxPostFader = false;

    DeviceInfo fx;
    fx.id = 701;
    fx.name = "Instrument";
    fx.pluginId = "instrument";
    fx.format = PluginFormat::VST3;
    fx.pluginState = "opaque-fx-state";
    preset.chain.fxChainElements.push_back(makeDeviceElement(fx));

    DeviceInfo nested = fx;
    nested.id = 706;
    nested.name = "Nested FX";
    nested.pluginState = "opaque-nested-state";
    ChainInfo chain;
    chain.id = 705;
    chain.name = "Parallel";
    chain.elements.push_back(makeDeviceElement(nested));
    RackInfo rack;
    rack.id = 704;
    rack.name = "Nested Rack";
    const auto nestedPresetPath =
        ChainNodePath::chainDevice(preset.id, rack.id, chain.id, nested.id);
    MacroInfo rackMacro(0);
    rackMacro.links.push_back({ControlTarget::pluginParam(nestedPresetPath, 4), 0.5f, false});
    rack.macros = {rackMacro};
    rack.chains.push_back(std::move(chain));
    preset.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    DeviceInfo post = fx;
    post.id = 702;
    post.name = "Post FX";
    post.pluginState = "opaque-post-state";
    preset.chain.postFxChainElements.push_back({post});

    DeviceInfo analysis = fx;
    analysis.id = 703;
    analysis.name = "Analysis";
    preset.chain.mixerAnalysisElements.push_back({analysis});

    MacroInfo macro(0);
    macro.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::topLevelDevice(preset.id, fx.id), 1), 1.0f,
         false});
    macro.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::postFxDevice(preset.id, post.id), 2), 1.0f,
         false});
    macro.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::mixerAnalysisDevice(preset.id, analysis.id), 3),
         1.0f, false});
    preset.macros = {macro};
    ModInfo mod(0);
    mod.links.push_back({ControlTarget::pluginParam(nestedPresetPath, 5), 0.75f, true});
    preset.mods = {mod};

    const auto createdId = fixture.tm().createTrackFromPreset(preset, "Emotional Violin");
    REQUIRE(createdId != INVALID_TRACK_ID);
    const auto* created = fixture.tm().getTrack(createdId);
    REQUIRE(created != nullptr);
    CHECK(created->name == "Emotional Violin");
    CHECK(created->type == TrackType::Media);
    CHECK(created->volume == preset.volume);
    CHECK(created->pan == preset.pan);
    CHECK(created->muted);
    CHECK_FALSE(created->chain.enabled);
    CHECK_FALSE(created->chain.postFxPostFader);
    REQUIRE(created->chain.fxChainElements.size() == 2);
    REQUIRE(created->chain.postFxChainElements.size() == 1);
    REQUIRE(created->chain.mixerAnalysisElements.size() == 1);

    const auto& liveFx = getDevice(created->chain.fxChainElements.front());
    const auto& liveRack = getRack(created->chain.fxChainElements[1]);
    REQUIRE(liveRack.chains.size() == 1);
    REQUIRE(liveRack.chains.front().elements.size() == 1);
    const auto& liveNested = getDevice(liveRack.chains.front().elements.front());
    const auto& livePost = created->chain.postFxChainElements.front().device;
    const auto& liveAnalysis = created->chain.mixerAnalysisElements.front().device;
    CHECK(liveFx.id != fx.id);
    CHECK(liveRack.id != 704);
    CHECK(liveRack.chains.front().id != 705);
    CHECK(liveNested.id != nested.id);
    CHECK(livePost.id != post.id);
    CHECK(liveAnalysis.id != analysis.id);
    CHECK(liveFx.pluginState == "opaque-fx-state");
    CHECK(liveNested.pluginState == "opaque-nested-state");
    CHECK(livePost.pluginState == "opaque-post-state");

    const auto liveNestedPath = ChainNodePath::chainDevice(
        createdId, liveRack.id, liveRack.chains.front().id, liveNested.id);
    REQUIRE(liveRack.macros.size() == 1);
    REQUIRE(liveRack.macros.front().links.size() == 1);
    CHECK(liveRack.macros.front().links.front().target.devicePath == liveNestedPath);
    REQUIRE(created->mods.size() == 1);
    REQUIRE(created->mods.front().links.size() == 1);
    CHECK(created->mods.front().links.front().target.devicePath == liveNestedPath);

    REQUIRE(created->macros.size() == 1);
    REQUIRE(created->macros.front().links.size() == 3);
    CHECK(created->macros.front().links[0].target.devicePath ==
          ChainNodePath::topLevelDevice(createdId, liveFx.id));
    CHECK(created->macros.front().links[1].target.devicePath ==
          ChainNodePath::postFxDevice(createdId, livePost.id));
    CHECK(created->macros.front().links[2].target.devicePath ==
          ChainNodePath::mixerAnalysisDevice(createdId, liveAnalysis.id));
}

TEST_CASE("Create-track-from-preset is one undoable mutation with stable redo identity",
          "[rack_audio][track_presets][create][undo]") {
    RackAudioTestFixture fixture;
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();

    TrackInfo preset;
    preset.name = "Preset";
    DeviceInfo device;
    device.id = 900;
    device.name = "Synth";
    device.pluginId = "synth";
    device.format = PluginFormat::VST3;
    device.pluginState = "opaque";
    preset.chain.fxChainElements.push_back(makeDeviceElement(device));

    auto command = std::make_unique<CreateTrackFromPresetCommand>(preset, "Remote Synth");
    auto* raw = command.get();
    undo.executeCommand(std::move(command));
    const auto createdId = raw->getCreatedTrackId();
    REQUIRE(createdId != INVALID_TRACK_ID);
    REQUIRE(fixture.tm().getTrack(createdId) != nullptr);
    CHECK(undo.getUndoDescription() == "Create Track from Preset");

    REQUIRE(undo.undo());
    CHECK(fixture.tm().getTrack(createdId) == nullptr);
    REQUIRE(undo.redo());
    const auto* restored = fixture.tm().getTrack(createdId);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->chain.fxChainElements.size() == 1);
    CHECK(getDevice(restored->chain.fxChainElements.front()).pluginState == "opaque");

    undo.clearHistory();
}

TEST_CASE("MAGDA duplicate track retargets copied macro and mod links",
          "[rack_audio][duplicate_track][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Duplicate Source");

    DeviceInfo topDevice;
    topDevice.name = "Top Delay";
    topDevice.format = PluginFormat::Internal;
    topDevice.pluginId = "delay";
    auto topDeviceId = fixture.tm().addDeviceToTrack(trackId, topDevice);
    auto topPath = ChainNodePath::topLevelDevice(trackId, topDeviceId);

    auto rackId = fixture.tm().addRackToTrack(trackId, "Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);
    auto* rack = fixture.tm().getRackByPath(rackPath);
    REQUIRE(rack != nullptr);
    REQUIRE(!rack->chains.empty());

    DeviceInfo rackDevice;
    rackDevice.name = "Rack EQ";
    rackDevice.format = PluginFormat::Internal;
    rackDevice.pluginId = "eq";
    rackDevice.pluginState = "<PLUGIN><MODIFIERASSIGNMENTS><LFO source=\"1\" paramID=\"freq\" "
                             "value=\"0.5\"/></MODIFIERASSIGNMENTS></PLUGIN>";

    auto chainId = rack->chains[0].id;
    auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
    auto rackDeviceId = fixture.tm().addDeviceToChainByPath(chainPath, rackDevice);
    auto rackDevicePath = ChainNodePath::chainDevice(trackId, rackId, chainId, rackDeviceId);

    fixture.tm().setMacroTarget(ChainNodePath::trackLevel(trackId), 0,
                                ControlTarget::pluginParam(topPath, 3));
    fixture.tm().setMacroLinkAmount(ChainNodePath::trackLevel(trackId), 0,
                                    ControlTarget::pluginParam(topPath, 3), 0.25f);
    fixture.tm().addMod(ChainNodePath::trackLevel(trackId), 0, ModType::LFO, LFOWaveform::Sine);
    fixture.tm().setModTarget(ChainNodePath::trackLevel(trackId), 0,
                              ControlTarget::pluginParam(topPath, 4));
    fixture.tm().setModLinkAmount(ChainNodePath::trackLevel(trackId), 0,
                                  ControlTarget::pluginParam(topPath, 4), 0.5f);

    fixture.tm().setMacroTarget(rackPath, 0, ControlTarget::pluginParam(rackDevicePath, 5));
    fixture.tm().setMacroLinkAmount(rackPath, 0, ControlTarget::pluginParam(rackDevicePath, 5),
                                    0.25f);

    auto* originalRackDevice = fixture.tm().getDeviceInChainByPath(rackDevicePath);
    REQUIRE(originalRackDevice != nullptr);
    fixture.tm().addMod(rackDevicePath, 0, ModType::LFO, LFOWaveform::Sine);
    fixture.tm().setModTarget(rackDevicePath, 0, ControlTarget::pluginParam(rackDevicePath, 6));
    fixture.tm().setModLinkAmount(rackDevicePath, 0, ControlTarget::pluginParam(rackDevicePath, 6),
                                  0.5f);

    auto* originalTopDevice = fixture.tm().getDeviceInChainByPath(topPath);
    REQUIRE(originalTopDevice != nullptr);
    ControlTarget legacyModRateTarget;
    legacyModRateTarget.kind = ControlTarget::Kind::ModParam;
    legacyModRateTarget.modId = 1;
    legacyModRateTarget.modParamIndex = 0;
    fixture.tm().setMacroTarget(topPath, 0, legacyModRateTarget);
    fixture.tm().setMacroLinkAmount(topPath, 0, legacyModRateTarget, 0.5f);

    auto duplicateTrackId = fixture.tm().duplicateTrack(trackId, true);
    REQUIRE(duplicateTrackId != INVALID_TRACK_ID);

    auto* duplicateTrack = fixture.tm().getTrack(duplicateTrackId);
    REQUIRE(duplicateTrack != nullptr);
    REQUIRE(duplicateTrack->chain.fxChainElements.size() == 2);
    REQUIRE(isDevice(duplicateTrack->chain.fxChainElements[0]));
    REQUIRE(isRack(duplicateTrack->chain.fxChainElements[1]));

    auto& duplicateTopDevice = getDevice(duplicateTrack->chain.fxChainElements[0]);
    auto duplicateTopPath = ChainNodePath::topLevelDevice(duplicateTrackId, duplicateTopDevice.id);
    REQUIRE(!duplicateTrack->macros.empty());
    REQUIRE(!duplicateTrack->mods.empty());
    REQUIRE(!duplicateTrack->macros[0].links.empty());
    REQUIRE(!duplicateTrack->mods[0].links.empty());
    CHECK(duplicateTrack->macros[0].links[0].target.devicePath == duplicateTopPath);
    CHECK(duplicateTrack->mods[0].links[0].target.devicePath == duplicateTopPath);
    REQUIRE(!duplicateTopDevice.macros.empty());
    REQUIRE(duplicateTopDevice.macros[0].links.size() == 1);
    CHECK(duplicateTopDevice.macros[0].links[0].target.isValid());
    CHECK(duplicateTopDevice.macros[0].links[0].target.devicePath == duplicateTopPath);
    CHECK(duplicateTopDevice.macros[0].links[0].target.kind == ControlTarget::Kind::ModParam);

    auto& duplicateRack = getRack(duplicateTrack->chain.fxChainElements[1]);
    REQUIRE(duplicateRack.chains.size() == 1);
    REQUIRE(duplicateRack.chains[0].elements.size() == 1);
    REQUIRE(isDevice(duplicateRack.chains[0].elements[0]));

    auto& duplicateRackDevice = getDevice(duplicateRack.chains[0].elements[0]);
    auto duplicateRackDevicePath = ChainNodePath::chainDevice(
        duplicateTrackId, duplicateRack.id, duplicateRack.chains[0].id, duplicateRackDevice.id);
    REQUIRE(!duplicateRack.macros.empty());
    REQUIRE(!duplicateRack.macros[0].links.empty());
    REQUIRE(!duplicateRackDevice.mods.empty());
    REQUIRE(!duplicateRackDevice.mods[0].links.empty());
    CHECK(duplicateRack.macros[0].links[0].target.devicePath == duplicateRackDevicePath);
    CHECK(duplicateRackDevice.mods[0].links[0].target.devicePath == duplicateRackDevicePath);
    CHECK(!duplicateRackDevice.pluginState.contains("MODIFIERASSIGNMENTS"));
}

// ============================================================================
// Rack Data Model Integration Tests
// ============================================================================

TEST_CASE("Rack audio sync: data model preparation", "[rack_audio][data_model]") {
    RackAudioTestFixture fixture;

    SECTION("Rack with devices has correct structure for sync") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "FX Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack != nullptr);
        REQUIRE(rack->chains.size() == 1);

        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        // Add devices to the chain
        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";

        DeviceInfo reverb;
        reverb.name = "Reverb";
        reverb.format = PluginFormat::Internal;
        reverb.pluginId = "reverb";

        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);
        auto reverbId = fixture.tm().addDeviceToChainByPath(chainPath, reverb);

        REQUIRE(delayId != INVALID_DEVICE_ID);
        REQUIRE(reverbId != INVALID_DEVICE_ID);

        // Verify the rack is in the track's chain elements
        auto* track = fixture.tm().getTrack(trackId);
        REQUIRE(track != nullptr);
        REQUIRE(track->chain.fxChainElements.size() == 1);
        REQUIRE(isRack(track->chain.fxChainElements[0]));

        const auto& rackElement = getRack(track->chain.fxChainElements[0]);
        REQUIRE(rackElement.id == rackId);
        REQUIRE(rackElement.chains[0].elements.size() == 2);
    }

    SECTION("Rack with multiple chains for parallel processing") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Parallel Rack");

        auto rackPath = ChainNodePath::rack(trackId, rackId);

        // Add a second chain
        auto chain2Id = fixture.tm().addChainToRack(rackPath, "Chain 2");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains.size() == 2);

        // Add different devices to each chain
        auto chain1Path = rackPath.withChain(rack->chains[0].id);
        auto chain2Path = rackPath.withChain(chain2Id);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";

        DeviceInfo reverb;
        reverb.name = "Reverb";
        reverb.format = PluginFormat::Internal;
        reverb.pluginId = "reverb";

        fixture.tm().addDeviceToChainByPath(chain1Path, delay);
        fixture.tm().addDeviceToChainByPath(chain2Path, reverb);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);
        REQUIRE(rack->chains[1].elements.size() == 1);
    }

    SECTION("Rack chain mute/solo state") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->chains[0].muted);
        REQUIRE_FALSE(rack->chains[0].solo);

        // Modify chain mute state
        rack->chains[0].muted = true;
        REQUIRE(rack->chains[0].muted);

        // Modify chain solo state
        rack->chains[0].solo = true;
        REQUIRE(rack->chains[0].solo);
    }

    SECTION("Rack bypass state") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->bypassed);

        fixture.tm().setRackBypassed(trackId, rackId, true);
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->bypassed);
    }

    SECTION("Rack chain volume and pan") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].volume == 0.0f);  // 0 dB (unity)
        REQUIRE(rack->chains[0].pan == 0.0f);     // Center

        // Set chain volume and pan
        rack->chains[0].volume = -6.0f;
        rack->chains[0].pan = 0.5f;

        REQUIRE(rack->chains[0].volume == -6.0f);
        REQUIRE(rack->chains[0].pan == 0.5f);
    }
}

TEST_CASE("Rack audio sync: macro and mod structure", "[rack_audio][macros][mods]") {
    RackAudioTestFixture fixture;

    SECTION("Rack has default macros") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->macros.size() == NUM_MACROS);
    }

    SECTION("Rack macro can link to device parameter") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";
        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);

        // Link macro 0 to the delay's parameter 0
        rack = fixture.tm().getRack(trackId, rackId);
        MacroLink link;
        link.target.devicePath = ChainNodePath::chainDevice(trackId, rackId, chainId, delayId);
        link.target.paramIndex = 0;
        link.amount = 0.75f;
        rack->macros[0].links.push_back(link);

        REQUIRE(rack->macros[0].isLinked());
        REQUIRE(rack->macros[0].links.size() == 1);
        REQUIRE(rack->macros[0].links[0].target.deviceId() == delayId);
    }

    SECTION("Rack mod can link to device parameter") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo eq;
        eq.name = "EQ";
        eq.format = PluginFormat::Internal;
        eq.pluginId = "eq";
        auto eqId = fixture.tm().addDeviceToChainByPath(chainPath, eq);

        // Add a default mod page so we have mods to work with
        rack = fixture.tm().getRack(trackId, rackId);
        addModPage(rack->mods);
        REQUIRE(rack->mods.size() > 0);

        ModLink link;
        link.target.devicePath = ChainNodePath::chainDevice(trackId, rackId, chainId, eqId);
        link.target.paramIndex = 0;
        link.amount = 0.5f;
        rack->mods[0].addLink(link.target, link.amount);

        REQUIRE(rack->mods[0].isLinked());
        REQUIRE(rack->mods[0].links.size() == 1);
    }
}

TEST_CASE("Rack audio sync: recursive device search", "[rack_audio][recursive_search]") {
    RackAudioTestFixture fixture;

    SECTION("Device inside rack is findable") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";
        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);

        // The device should be findable via the path resolution
        auto devicePath = chainPath.withDevice(delayId);
        auto* foundDevice = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(foundDevice != nullptr);
        REQUIRE(foundDevice->name == "Delay");
    }

    SECTION("Top-level device coexists with rack") {
        auto trackId = fixture.tm().createTrack("Test Track");

        // Add a top-level device first
        DeviceInfo topDevice;
        topDevice.name = "Top EQ";
        topDevice.format = PluginFormat::Internal;
        topDevice.pluginId = "eq";
        fixture.tm().addDeviceToTrack(trackId, topDevice);

        // Add a rack
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* track = fixture.tm().getTrack(trackId);
        REQUIRE(track->chain.fxChainElements.size() == 2);
        REQUIRE(isDevice(track->chain.fxChainElements[0]));
        REQUIRE(isRack(track->chain.fxChainElements[1]));
    }
}

// ============================================================================
// One recursive re-key (#2221)
//
// Duplication, preset import, rack presets and chain presets each grew their
// own, and two of the four stopped at the device rather than descending into
// the pads it owns. A preset carrying a Drum Grid therefore brought the
// preset's pad DeviceIds into a live project, where they collide with whatever
// already holds them, and left the pad rack id derived from the id the grid had
// in the preset rather than the one it has now.
// ============================================================================

namespace {

/// A Drum Grid carrying one pad device, both under ids the caller chooses.
DeviceInfo presetGridWithPad(DeviceId gridId, DeviceId padDeviceId) {
    DeviceInfo grid;
    grid.id = gridId;
    grid.name = "Drum Grid";
    grid.pluginId = "drumgrid";
    grid.format = PluginFormat::Internal;
    grid.isInstrument = true;
    grid.deviceType = DeviceType::Instrument;

    auto& pads = ensurePads(grid);
    ChainInfo pad;
    pad.id = 1;

    DeviceInfo voice;
    voice.id = padDeviceId;
    voice.name = "Kick";
    voice.pluginId = "magdasampler";
    voice.format = PluginFormat::Internal;
    voice.isInstrument = true;
    voice.deviceType = DeviceType::Instrument;
    pad.elements.push_back(makeDeviceElement(voice));

    pads.chains.push_back(std::move(pad));
    stampPadRackId(grid);
    return grid;
}

}  // namespace

TEST_CASE("A rack preset re-keys the pads of a device it carries",
          "[rack_audio][rack_presets][pads]") {
    RackAudioTestFixture fixture;
    auto& tm = fixture.tm();

    const auto trackId = tm.createTrack("Rack Preset Target");
    const auto liveRackId = tm.addRackToTrack(trackId, "Live Rack");
    const auto liveRackPath = ChainNodePath::rack(trackId, liveRackId);

    // A live device whose id the preset's pad device also claims, so leaving the
    // pad un-keyed puts two devices in one project under one id.
    DeviceInfo occupant;
    occupant.name = "Delay";
    occupant.pluginId = "delay";
    occupant.format = PluginFormat::Internal;
    const auto occupantId = tm.addDeviceToTrack(trackId, occupant);
    REQUIRE(occupantId != INVALID_DEVICE_ID);

    RackInfo presetRack;
    presetRack.id = 90;
    presetRack.name = "Preset Rack";
    ChainInfo presetChain;
    presetChain.id = 91;
    presetChain.elements.push_back(makeDeviceElement(presetGridWithPad(92, occupantId)));
    presetRack.chains.push_back(std::move(presetChain));

    REQUIRE(tm.applyRackPreset(liveRackPath, presetRack));

    auto* liveRack = tm.getRackByPath(liveRackPath);
    REQUIRE(liveRack != nullptr);
    REQUIRE(liveRack->chains.size() == 1);
    REQUIRE(liveRack->chains[0].elements.size() == 1);

    const auto& grid = getDevice(liveRack->chains[0].elements[0]);
    REQUIRE(static_cast<bool>(grid.pads));
    REQUIRE(grid.pads->chains.size() == 1);
    REQUIRE(grid.pads->chains[0].elements.size() == 1);
    const auto& padDevice = getDevice(grid.pads->chains[0].elements[0]);

    // The grid got a fresh id, and so did the device on its pad.
    CHECK(grid.id != 92);
    CHECK(padDevice.id != occupantId);
    CHECK(padDevice.id != grid.id);

    // And the pad rack id follows the id the grid has now, which is what every
    // pad path is built from.
    CHECK(grid.pads->id == padRackIdFor(grid.id));
}

TEST_CASE("A chain preset re-keys the pads of a device it carries",
          "[rack_audio][track_presets][pads]") {
    RackAudioTestFixture fixture;
    auto& tm = fixture.tm();

    const auto trackId = tm.createTrack("Chain Preset Target");

    DeviceInfo occupant;
    occupant.name = "Delay";
    occupant.pluginId = "delay";
    occupant.format = PluginFormat::Internal;
    const auto occupantId = tm.addDeviceToTrack(trackId, occupant);
    REQUIRE(occupantId != INVALID_DEVICE_ID);

    std::vector<ChainElement> presetElements;
    presetElements.push_back(makeDeviceElement(presetGridWithPad(200, occupantId)));

    REQUIRE(tm.applyChainPreset(trackId, std::move(presetElements)));

    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);

    const auto& grid = getDevice(track->chain.fxChainElements[0]);
    REQUIRE(static_cast<bool>(grid.pads));
    REQUIRE(grid.pads->chains.size() == 1);
    REQUIRE(grid.pads->chains[0].elements.size() == 1);
    const auto& padDevice = getDevice(grid.pads->chains[0].elements[0]);

    CHECK(grid.id != 200);
    CHECK(padDevice.id != occupantId);
    CHECK(padDevice.id != grid.id);
    CHECK(grid.pads->id == padRackIdFor(grid.id));
}
