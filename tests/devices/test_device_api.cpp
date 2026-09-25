#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <set>

#include "magda/daw/api/device_api_live.hpp"
#include "magda/daw/api/track_api_live.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/PresetManager.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

/// A catalog id that exists in every build, so add tests need no plugin scan.
juce::String anyCatalogId() {
    const DeviceApiLive devices;
    const auto catalog = devices.getCatalog();
    REQUIRE_FALSE(catalog.empty());
    return catalog.front().catalogId;
}

TrackId freshTrack(const juce::String& name) {
    auto& tracks = TrackManager::getInstance();
    AutomationManager::getInstance().clearAll();
    tracks.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    return tracks.createTrack(name, TrackType::Media);
}

struct PresetDirectoryScope {
    PresetDirectoryScope()
        : previous(Config::getInstance().getPresetsDir()),
          directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("magda-device-api-presets", {})) {
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

}  // namespace

TEST_CASE("The device catalog is discoverable and internally consistent", "[device-api][catalog]") {
    const DeviceApiLive devices;
    const auto catalog = devices.getCatalog();

    // The compiled and internal registries are linked into this binary, so the
    // catalog is never empty even with no plugin scan.
    REQUIRE_FALSE(catalog.empty());

    std::set<juce::String> ids;
    for (const auto& entry : catalog) {
        INFO("catalog entry: " << entry.name);
        // A caller addresses a device by this and nothing else.
        REQUIRE(entry.catalogId.isNotEmpty());
        // Ids must be unique or addDevice would be ambiguous.
        REQUIRE(ids.insert(entry.catalogId).second);
        REQUIRE(entry.isInstrument == (entry.type == DeviceType::Instrument));
    }
}

TEST_CASE("Every catalog entry resolves by its own id", "[device-api][catalog]") {
    const DeviceApiLive devices;
    for (const auto& entry : devices.getCatalog()) {
        const auto found = devices.findCatalogEntry(entry.catalogId);
        REQUIRE(found.has_value());
        REQUIRE(*found == entry);
    }
}

TEST_CASE("Unknown catalog ids resolve to nothing", "[device-api][catalog]") {
    const DeviceApiLive devices;
    REQUIRE_FALSE(devices.findCatalogEntry("not_a_device").has_value());
    REQUIRE_FALSE(devices.findCatalogEntry("").has_value());
}

TEST_CASE("A MAGDA preset applies in place as one no-op-aware undo step",
          "[device-api][presets][2836]") {
    PresetDirectoryScope presetDirectory;
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Preset target");

    DeviceInfo initial;
    initial.name = "Remote Preset Test";
    initial.pluginId = "remote_preset_test";
    initial.format = PluginFormat::Internal;
    initial.bypassed = true;
    ParameterInfo tone;
    tone.paramIndex = 0;
    tone.name = "Tone";
    tone.currentValue = 0.25f;
    initial.parameters.push_back(tone);
    const auto deviceId = tracks.addDeviceToTrack(trackId, initial);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    const auto path = ChainNodePath::topLevelDevice(trackId, deviceId);
    const auto* before = tracks.getDeviceInChainByPath(path);
    REQUIRE(before != nullptr);

    auto preset = *before;
    preset.id = 999;
    preset.parameters[0].currentValue = 0.75f;
    preset.bypassed = false;  // Slot state, deliberately not preset state.
    REQUIRE(PresetManager::getInstance().saveDevicePreset(preset, "Warm"));

    MacroInfo trackMacro(0);
    trackMacro.links.push_back({ControlTarget::pluginParam(path, 0), 1.0f, false});
    tracks.getTrack(trackId)->macros = {trackMacro};

    DeviceApiLive api;
    const auto listed = api.getDevicePresets(path);
    REQUIRE(listed.size() == 1);
    const auto result = api.applyPreset(path, listed.front().id);
    REQUIRE(result.status == ApplyDevicePresetStatus::Applied);

    const auto* applied = tracks.getDeviceInChainByPath(path);
    REQUIRE(applied != nullptr);
    CHECK(applied->id == deviceId);
    CHECK(applied->bypassed);
    REQUIRE(applied->parameters.size() == 1);
    CHECK(applied->parameters[0].paramIndex == 0);
    CHECK(applied->parameters[0].currentValue == 0.75f);
    REQUIRE(result.referenceImpact.preserved.size() == 1);
    CHECK(tracks.getTrack(trackId)->macros[0].links[0].target.paramIndex == 0);
    CHECK(UndoManager::getInstance().getUndoDescription() == "Apply Device Preset");

    const auto noOp = api.applyPreset(path, listed.front().id);
    CHECK(noOp.status == ApplyDevicePresetStatus::Unchanged);
    REQUIRE(UndoManager::getInstance().undo());
    CHECK_FALSE(UndoManager::getInstance().canUndo());
    const auto* restored = tracks.getDeviceInChainByPath(path);
    REQUIRE(restored != nullptr);
    CHECK(restored->id == deviceId);
    CHECK(restored->bypassed);
    REQUIRE(restored->parameters.size() == 1);
    CHECK(restored->parameters[0].paramIndex == 0);
    CHECK(restored->parameters[0].currentValue == 0.25f);
    CHECK(tracks.getTrack(trackId)->macros[0].links[0].target.paramIndex == 0);

    auto conflicting = *restored;
    conflicting.parameters.clear();
    REQUIRE(PresetManager::getInstance().saveDevicePreset(conflicting, "Missing Tone"));
    const auto relisted = api.getDevicePresets(path);
    const auto missingTone =
        std::ranges::find(relisted, juce::String("Missing Tone"), &DevicePresetEntry::name);
    REQUIRE(missingTone != relisted.end());
    const auto rejected = api.applyPreset(path, missingTone->id);
    CHECK(rejected.status == ApplyDevicePresetStatus::ReferenceConflict);
    REQUIRE(rejected.referenceImpact.rejected.size() == 1);
    CHECK_FALSE(UndoManager::getInstance().canUndo());
    REQUIRE(tracks.getDeviceInChainByPath(path)->parameters.size() == 1);
    CHECK(tracks.getTrack(trackId)->macros[0].links[0].target.paramIndex == 0);
}

TEST_CASE("The preset command remaps stable parameter references on execute and undo",
          "[device-api][presets][2836]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Preset remap");

    DeviceInfo initial;
    initial.name = "Remappable";
    initial.pluginId = "remappable";
    ParameterInfo parameter;
    parameter.paramIndex = 0;
    parameter.stableId = "tone";
    initial.parameters = {parameter};
    const auto deviceId = tracks.addDeviceToTrack(trackId, initial);
    const auto path = ChainNodePath::topLevelDevice(trackId, deviceId);

    MacroInfo macro(0);
    macro.links.push_back({ControlTarget::pluginParam(path, 0), 1.0f, false});
    tracks.getTrack(trackId)->macros = {macro};

    auto preset = *tracks.getDeviceInChainByPath(path);
    preset.parameters[0].paramIndex = 7;
    UndoManager::getInstance().executeCommand(std::make_unique<ApplyDevicePresetCommand>(
        path, preset, std::vector<std::pair<int, int>>{{0, 7}}));
    CHECK(tracks.getDeviceInChainByPath(path)->parameters[0].paramIndex == 7);
    CHECK(tracks.getTrack(trackId)->macros[0].links[0].target.paramIndex == 7);

    REQUIRE(UndoManager::getInstance().undo());
    CHECK(tracks.getDeviceInChainByPath(path)->parameters[0].paramIndex == 0);
    CHECK(tracks.getTrack(trackId)->macros[0].links[0].target.paramIndex == 0);
}

TEST_CASE("A track preset replaces only chain-owned state and remaps stable references",
          "[track-api][presets][2839]") {
    PresetDirectoryScope presetDirectory;
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Keep This Track");
    const auto sourceTrackId = tracks.createTrack("Sidechain Source", TrackType::Media);
    const auto routedTrackId = tracks.createTrack("Routed Child", TrackType::MultiOut);

    auto* target = tracks.getTrack(trackId);
    REQUIRE(target != nullptr);
    target->colour = juce::Colours::orange;
    target->volume = 0.42f;
    target->manualVolume = 0.42f;
    target->pan = -0.25f;
    target->recordArmed = true;
    target->inputMonitor = InputMonitorMode::In;
    target->midiInputDevice = "all";
    target->audioOutputDevice = "master";
    target->sends = {{2, 0.6f, true, sourceTrackId}};
    target->parentId = 77;

    DeviceInfo original;
    original.name = "Stable Synth";
    original.pluginId = "stable_synth";
    original.format = PluginFormat::Internal;
    original.sidechain.type = SidechainConfig::Type::Audio;
    original.sidechain.sourceTrackId = sourceTrackId;
    ParameterInfo oldCutoff;
    oldCutoff.paramIndex = 0;
    oldCutoff.stableId = "cutoff";
    oldCutoff.currentValue = 0.2f;
    original.parameters = {oldCutoff};
    const auto originalId = tracks.addDeviceToTrack(trackId, original);
    const auto originalPath = ChainNodePath::topLevelDevice(trackId, originalId);

    auto* routed = tracks.getTrack(routedTrackId);
    REQUIRE(routed != nullptr);
    routed->multiOutLink = MultiOutTrackLink{trackId, originalId, 1};
    auto& automation = AutomationManager::getInstance();
    const auto laneId =
        automation.createLane(ControlTarget::pluginParam(originalPath, oldCutoff.paramIndex),
                              AutomationLaneType::Absolute);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);

    DeviceInfo analysis;
    analysis.id = 500;
    analysis.name = "Rail Analysis";
    analysis.pluginId = "scope";
    target = tracks.getTrack(trackId);
    REQUIRE(target != nullptr);
    target->chain.mixerAnalysisElements.push_back({analysis});

    auto* source = tracks.getTrack(sourceTrackId);
    REQUIRE(source != nullptr);
    MacroInfo externalMacro(0);
    externalMacro.links.push_back(
        {ControlTarget::pluginParam(originalPath, oldCutoff.paramIndex), 1.0f, false});
    source->macros = {externalMacro};

    TrackInfo preset;
    preset.id = 999;
    preset.name = "Do Not Replace Name";
    preset.volume = 0.1f;
    preset.parentId = INVALID_TRACK_ID;
    preset.chain.enabled = false;
    preset.chain.postFxPostFader = false;
    DeviceInfo replacement = original;
    replacement.id = 700;
    replacement.sidechain = {};
    replacement.parameters[0].paramIndex = 7;
    replacement.parameters[0].currentValue = 0.8f;
    preset.chain.fxChainElements.push_back(makeDeviceElement(replacement));
    REQUIRE(PresetManager::getInstance().saveChainPreset(preset, "Remote Chain"));

    const auto listed = PresetManager::getInstance().getTrackPresetMetadata();
    REQUIRE(listed.size() == 1);
    TrackApiLive api;
    const auto result = api.applyPreset(trackId, listed.front().id);
    REQUIRE(result.status == ApplyTrackPresetStatus::Applied);
    REQUIRE(result.referenceImpact.remapped.size() == 3);
    REQUIRE(result.referenceImpact.dropped.size() == 1);

    const auto* applied = tracks.getTrack(trackId);
    REQUIRE(applied != nullptr);
    CHECK(applied->id == trackId);
    CHECK(applied->name == "Keep This Track");
    CHECK(applied->colour == juce::Colours::orange);
    CHECK(applied->volume == 0.42f);
    CHECK(applied->pan == -0.25f);
    CHECK(applied->recordArmed);
    CHECK(applied->inputMonitor == InputMonitorMode::In);
    CHECK(applied->midiInputDevice == "all");
    CHECK(applied->audioOutputDevice == "master");
    CHECK(applied->sends.size() == 1);
    CHECK(applied->parentId == 77);
    CHECK_FALSE(applied->chain.enabled);
    CHECK_FALSE(applied->chain.postFxPostFader);
    REQUIRE(applied->chain.mixerAnalysisElements.size() == 1);
    CHECK(applied->chain.mixerAnalysisElements.front().device.id == 500);
    REQUIRE(applied->chain.fxChainElements.size() == 1);
    const auto& liveReplacement = getDevice(applied->chain.fxChainElements.front());
    CHECK(liveReplacement.id != originalId);
    REQUIRE(liveReplacement.parameters.size() == 1);
    CHECK(liveReplacement.parameters.front().paramIndex == 7);
    CHECK(liveReplacement.parameters.front().currentValue == 0.8f);
    const auto replacementPath = ChainNodePath::topLevelDevice(trackId, liveReplacement.id);
    REQUIRE(source->macros.front().links.size() == 1);
    CHECK(source->macros.front().links.front().target.devicePath == replacementPath);
    CHECK(source->macros.front().links.front().target.paramIndex == 7);
    REQUIRE(automation.getLane(laneId) != nullptr);
    CHECK(automation.getLane(laneId)->target.devicePath == replacementPath);
    CHECK(automation.getLane(laneId)->target.paramIndex == 7);
    REQUIRE(routed->multiOutLink.has_value());
    CHECK(routed->multiOutLink->sourceTrackId == trackId);
    CHECK(routed->multiOutLink->sourceDeviceId == liveReplacement.id);
    CHECK(UndoManager::getInstance().getUndoDescription() == "Apply Track Preset");

    REQUIRE(UndoManager::getInstance().undo());
    const auto* restored = tracks.getTrack(trackId);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->chain.fxChainElements.size() == 1);
    CHECK(getDevice(restored->chain.fxChainElements.front()).id == originalId);
    CHECK(source->macros.front().links.front().target.devicePath == originalPath);
    CHECK(source->macros.front().links.front().target.paramIndex == 0);
    REQUIRE(automation.getLane(laneId) != nullptr);
    CHECK(automation.getLane(laneId)->target.devicePath == originalPath);
    CHECK(automation.getLane(laneId)->target.paramIndex == 0);
    REQUIRE(routed->multiOutLink.has_value());
    CHECK(routed->multiOutLink->sourceDeviceId == originalId);

    UndoManager::getInstance().clearHistory();
    TrackInfo conflictingPreset;
    DeviceInfo unrelated;
    unrelated.id = 800;
    unrelated.name = "Different Synth";
    unrelated.pluginId = "different_synth";
    unrelated.parameters = {oldCutoff};
    conflictingPreset.chain.fxChainElements.push_back(makeDeviceElement(unrelated));
    REQUIRE(PresetManager::getInstance().saveChainPreset(conflictingPreset, "Conflict"));
    const auto relisted = PresetManager::getInstance().getTrackPresetMetadata();
    const auto conflict = std::ranges::find(relisted, juce::String("Conflict"),
                                            &PresetManager::TrackPresetMetadata::name);
    REQUIRE(conflict != relisted.end());
    const auto rejected = api.applyPreset(trackId, conflict->id);
    CHECK(rejected.status == ApplyTrackPresetStatus::ReferenceConflict);
    CHECK_FALSE(rejected.referenceImpact.rejected.empty());
    CHECK_FALSE(UndoManager::getInstance().canUndo());
    CHECK(getDevice(tracks.getTrack(trackId)->chain.fxChainElements.front()).id == originalId);
    CHECK(source->macros.front().links.front().target.devicePath == originalPath);

    source->macros.clear();
    automation.clearAll();
    routed->multiOutLink.reset();
    TrackInfo incompatiblePreset;
    DeviceInfo postFxInstrument;
    postFxInstrument.id = 900;
    postFxInstrument.name = "Invalid Post-FX Instrument";
    postFxInstrument.pluginId = "invalid_post_fx_instrument";
    postFxInstrument.isInstrument = true;
    incompatiblePreset.chain.postFxChainElements.push_back({postFxInstrument});
    REQUIRE(PresetManager::getInstance().saveChainPreset(incompatiblePreset, "Incompatible"));
    const auto finalList = PresetManager::getInstance().getTrackPresetMetadata();
    const auto incompatible = std::ranges::find(finalList, juce::String("Incompatible"),
                                                &PresetManager::TrackPresetMetadata::name);
    REQUIRE(incompatible != finalList.end());
    CHECK(api.applyPreset(trackId, incompatible->id).status ==
          ApplyTrackPresetStatus::Incompatible);
    CHECK_FALSE(UndoManager::getInstance().canUndo());
    CHECK(getDevice(tracks.getTrack(trackId)->chain.fxChainElements.front()).id == originalId);
}

TEST_CASE("Live device lookup rejects paths that address nothing", "[device-api][inspection]") {
    const DeviceApiLive devices;

    // A default path names no node at all.
    REQUIRE(devices.getDevice(ChainNodePath{}) == nullptr);
    REQUIRE(devices.getDeviceParameters(ChainNodePath{}).empty());

    // A well-formed path to a track that does not exist.
    const auto missing = ChainNodePath::topLevelDevice(9999, 1);
    REQUIRE(devices.getDevice(missing) == nullptr);
    REQUIRE(devices.getDeviceParameters(missing).empty());
}

TEST_CASE("Each device added from a reusable template gets an id of its own",
          "[device-api][inspection]") {
    // A browser entry is a template added over and over. Each placement is its
    // own device: it runs its own plugin instance and keys its own op.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Track");

    DeviceInfo browserTemplate;
    browserTemplate.name = "Reusable effect";
    browserTemplate.pluginId = "template-effect";

    const auto firstId = tracks.addDeviceToTrack(trackId, browserTemplate);
    const auto secondId = tracks.addDeviceToTrack(trackId, browserTemplate);
    REQUIRE(firstId != INVALID_DEVICE_ID);
    REQUIRE(secondId != INVALID_DEVICE_ID);
    CHECK(firstId != secondId);

    CHECK(tracks.getDevice(trackId, firstId) != nullptr);
    CHECK(tracks.getDevice(trackId, secondId) != nullptr);
}

TEST_CASE("The three sections hand out the same DeviceId", "[device-api][inspection]") {
    // Not an accident to be fixed: the counters are section-local by design,
    // and this is why runtime ownership keys on engine::DeviceKey rather than
    // on a bare id, and why findUniqueBareDeviceIdMatch has to exist (#2261).
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Track");

    DeviceInfo effect;
    effect.name = "Reusable effect";
    effect.pluginId = "template-effect";

    DeviceInfo analysis;
    analysis.name = "Reusable analyser";
    analysis.pluginId = "template-analyser";

    const auto topLevelId = tracks.addDeviceToTrack(trackId, effect);
    const auto postFxId = tracks.addDeviceToPostFx(trackId, effect);
    const auto analysisId = tracks.addDeviceToMixerAnalysis(trackId, analysis);

    REQUIRE(topLevelId != INVALID_DEVICE_ID);
    CHECK(postFxId == topLevelId);
    CHECK(analysisId == topLevelId);
}

TEST_CASE("A duplicated track's devices are re-keyed away from the originals",
          "[device-api][inspection]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Track");

    DeviceInfo effect;
    effect.name = "Reusable effect";
    effect.pluginId = "template-effect";

    const auto sourceId = tracks.addDeviceToTrack(trackId, effect);
    REQUIRE(sourceId != INVALID_DEVICE_ID);
    REQUIRE(tracks.addDeviceToPostFx(trackId, effect) != INVALID_DEVICE_ID);

    const auto sourcePostFxId = tracks.getPostFxChainElements(trackId).front().device.id;

    const auto copyId = tracks.duplicateTrack(trackId);
    REQUIRE(copyId != INVALID_TRACK_ID);

    // The copy runs its own plugin instances, so sharing an id would mean
    // sharing an op and, once the native engine binds them, a plugin.
    const auto& copiedTopLevel = tracks.getChainElements(copyId);
    REQUIRE_FALSE(copiedTopLevel.empty());
    REQUIRE(magda::isDevice(copiedTopLevel.front()));
    CHECK(magda::getDevice(copiedTopLevel.front()).id != sourceId);

    const auto& copiedPostFx = tracks.getPostFxChainElements(copyId);
    REQUIRE(copiedPostFx.size() == 1);
    CHECK(copiedPostFx.front().device.id != sourcePostFxId);
}

TEST_CASE("A pad's nested rack is re-keyed along with the pad itself", "[device-api][inspection]") {
    // Pads hold chain elements like any other chain, so a pre-populated Drum
    // Grid can arrive with a rack inside a pad. A walk that stopped at the
    // direct pad devices left everything under that rack carrying the source's
    // DeviceIds, keying the original's ops.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Track");

    DeviceInfo nested;
    nested.name = "Nested effect";
    nested.pluginId = "template-effect";
    nested.id = 4242;

    auto rack = std::make_unique<RackInfo>();
    rack->id = 9911;
    ChainInfo rackChain;
    rackChain.id = 8811;
    rackChain.elements.push_back(ChainElement{nested});
    rack->chains.push_back(std::move(rackChain));

    DeviceInfo grid;
    grid.name = "Grid";
    grid.pluginId = "drumgrid";
    auto& pads = magda::ensurePads(grid);
    magda::ensurePadChain(pads, 0).elements.push_back(ChainElement{std::move(rack)});

    const auto gridId = tracks.addDeviceToTrack(trackId, grid);
    REQUIRE(gridId != INVALID_DEVICE_ID);

    const auto* live = tracks.getDevice(trackId, gridId);
    REQUIRE(live != nullptr);
    REQUIRE(static_cast<bool>(live->pads));
    REQUIRE(live->pads->chains.size() == 1);

    const auto& padElements = live->pads->chains.front().elements;
    REQUIRE(padElements.size() == 1);
    REQUIRE(magda::isRack(padElements.front()));

    const auto& liveRack = magda::getRack(padElements.front());
    CHECK(liveRack.id != 9911);
    REQUIRE(liveRack.chains.size() == 1);
    CHECK(liveRack.chains.front().id != 8811);

    const auto& nestedElements = liveRack.chains.front().elements;
    REQUIRE(nestedElements.size() == 1);
    REQUIRE(magda::isDevice(nestedElements.front()));

    const auto& liveNested = magda::getDevice(nestedElements.front());
    CHECK(liveNested.id != 4242);
}

TEST_CASE("A grid's links follow its pads when the grid is placed", "[device-api][inspection]") {
    // A pre-populated Drum Grid arrives with links already pointing into its
    // pads: the grid's own macro drives a pad device's parameter, and that pad
    // device's macro drives the same one. Placing it re-keys the whole pad
    // subtree, and a link left on the old address resolves to nothing -- the
    // macro is still there, still says it is linked, and moves nothing.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Track");

    constexpr DeviceId kGridId = 7000;
    constexpr DeviceId kPadDeviceId = 7001;
    constexpr ChainId kPadChainId = 1;

    DeviceInfo padDevice;
    padDevice.name = "Pad effect";
    padDevice.pluginId = "template-effect";
    padDevice.id = kPadDeviceId;

    const auto sourcePadDevicePath =
        ChainNodePath::padChain(trackId, kGridId, kPadChainId).withDevice(kPadDeviceId);

    // The pad device's own macro, pointing at itself: a link that lives inside
    // the subtree and names an id the re-key moves.
    MacroLink self;
    self.target.devicePath = sourcePadDevicePath;
    self.target.paramIndex = 3;
    self.amount = 1.0f;
    padDevice.macros.front().links.push_back(self);

    DeviceInfo grid;
    grid.name = "Grid";
    grid.pluginId = "drumgrid";
    grid.id = kGridId;
    auto& pads = magda::ensurePads(grid);
    auto& pad = magda::ensurePadChain(pads, 0);
    pad.id = kPadChainId;
    pad.elements.push_back(ChainElement{padDevice});

    // The grid's macro, pointing down into the pad.
    MacroLink intoPad;
    intoPad.target.devicePath = sourcePadDevicePath;
    intoPad.target.paramIndex = 3;
    intoPad.amount = 1.0f;
    grid.macros.front().links.push_back(intoPad);

    // The pad rack owns macros and mods of its own -- it is a RackInfo, a pad
    // path resolves to it, and the modulation surfaces read its macros like any
    // other rack's. A walk that descended straight into its chains left these
    // behind.
    if (pads.macros.empty())
        pads.macros.emplace_back(0);
    pads.macros.front().links.push_back(intoPad);

    const auto gridId = tracks.addDeviceToTrack(trackId, grid);
    REQUIRE(gridId != INVALID_DEVICE_ID);
    REQUIRE(gridId != kGridId);

    const auto* live = tracks.getDevice(trackId, gridId);
    REQUIRE(live != nullptr);
    REQUIRE(static_cast<bool>(live->pads));
    REQUIRE(live->pads->chains.size() == 1);

    const auto& liveElements = live->pads->chains.front().elements;
    REQUIRE(liveElements.size() == 1);
    REQUIRE(magda::isDevice(liveElements.front()));
    const auto& livePadDevice = magda::getDevice(liveElements.front());
    REQUIRE(livePadDevice.id != kPadDeviceId);

    const auto livePadDevicePath =
        ChainNodePath::padChain(trackId, gridId, live->pads->chains.front().id)
            .withDevice(livePadDevice.id);

    REQUIRE_FALSE(live->macros.front().links.empty());
    CHECK(live->macros.front().links.front().target.devicePath == livePadDevicePath);

    REQUIRE_FALSE(livePadDevice.macros.front().links.empty());
    CHECK(livePadDevice.macros.front().links.front().target.devicePath == livePadDevicePath);

    REQUIRE_FALSE(live->pads->macros.empty());
    REQUIRE_FALSE(live->pads->macros.front().links.empty());
    CHECK(live->pads->macros.front().links.front().target.devicePath == livePadDevicePath);
}

TEST_CASE("Device parameters are reported in real units", "[device-api][inspection]") {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Synth", TrackType::Media);

    DeviceInfo device;
    device.name = "Filter";
    device.pluginId = "test_filter";
    ParameterInfo cutoff;
    cutoff.paramIndex = 0;
    cutoff.stableId = "cutoff";
    cutoff.name = "Cutoff";
    cutoff.unit = "Hz";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.defaultValue = 1000.0f;
    cutoff.currentValue = 440.0f;
    device.parameters.push_back(cutoff);

    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const DeviceApiLive devices;
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());
    REQUIRE(devices.getDevice(path) != nullptr);

    const auto parameters = devices.getDeviceParameters(path);
    REQUIRE(parameters.size() == 1);
    // Real parameter units, not MAGDA-normalized automation units.
    REQUIRE(parameters[0].name == "Cutoff");
    REQUIRE(parameters[0].unit == "Hz");
    REQUIRE(parameters[0].minValue == 20.0f);
    REQUIRE(parameters[0].maxValue == 20000.0f);
    REQUIRE(parameters[0].currentValue == 440.0f);
    REQUIRE(parameters[0].stableId == "cutoff");

    tracks.clearAllTracks();
}

// ============================================================================
// Mutations
// ============================================================================

TEST_CASE("Adding a device by catalog id is one undo step", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    const auto catalogId = anyCatalogId();

    DeviceApiLive devices;
    const auto deviceId =
        devices.addDevice(ChainNodePath::trackLevel(trackId), catalogId, /*index=*/-1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto& tracks = TrackManager::getInstance();
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());
    const auto* added = devices.getDevice(path);
    REQUIRE(added != nullptr);
    REQUIRE(added->pluginId == catalogId);

    // One Undo removes it, and one Redo brings it back.
    auto& undo = UndoManager::getInstance();
    REQUIRE(undo.canUndo());
    undo.undo();
    REQUIRE(tracks.getChainElements(trackId).empty());

    undo.redo();
    REQUIRE(tracks.getChainElements(trackId).size() == 1);

    tracks.clearAllTracks();
}

TEST_CASE("Adding a device rejects placements that do not resolve", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    const auto catalogId = anyCatalogId();
    DeviceApiLive devices;

    SECTION("unknown catalog id") {
        REQUIRE(devices.addDevice(ChainNodePath::trackLevel(trackId), "not_a_device", -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("track that does not exist") {
        REQUIRE(devices.addDevice(ChainNodePath::trackLevel(9999), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("a device path is not a placement") {
        // Only a track or a chain can hold a device.
        REQUIRE(devices.addDevice(ChainNodePath::topLevelDevice(trackId, 1), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("chain that does not exist") {
        REQUIRE(devices.addDevice(ChainNodePath::chain(trackId, 42, 43), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Removing a device restores it on undo", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto& tracks = TrackManager::getInstance();
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(devices.removeDevice(path));
    REQUIRE(tracks.getChainElements(trackId).empty());

    UndoManager::getInstance().undo();
    REQUIRE(tracks.getChainElements(trackId).size() == 1);

    // Removing something that is not there is a failure, not a silent no-op.
    REQUIRE_FALSE(devices.removeDevice(ChainNodePath::topLevelDevice(trackId, 9999)));
    REQUIRE_FALSE(devices.removeDevice(ChainNodePath{}));

    tracks.clearAllTracks();
}

TEST_CASE("Undoing a removal restores the device under its original id",
          "[device-api][mutation][undo]") {
    // The ordinary add path stamps a fresh DeviceId via prepareNewDevice. If
    // undo went through it, the device would come back under a different id and
    // every automation lane, macro link, and alias targeting it would dangle.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;

    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());

    REQUIRE(devices.removeDevice(path));
    UndoManager::getInstance().undo();

    // Same id, and therefore still reachable at the path that addressed it.
    REQUIRE(tracks.findDevicePath(deviceId) == path);
    REQUIRE(devices.getDevice(path) != nullptr);
    REQUIRE(devices.getDevice(path)->id == deviceId);

    tracks.clearAllTracks();
}

TEST_CASE("Undo restores a device to its original position", "[device-api][mutation][undo]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto catalogId = anyCatalogId();
    const auto trackPath = ChainNodePath::trackLevel(trackId);

    const auto first = devices.addDevice(trackPath, catalogId, -1);
    const auto middle = devices.addDevice(trackPath, catalogId, -1);
    const auto last = devices.addDevice(trackPath, catalogId, -1);
    REQUIRE(tracks.getChainElements(trackId).size() == 3);

    REQUIRE(devices.removeDevice(tracks.findDevicePath(middle)));
    REQUIRE(tracks.getChainElements(trackId).size() == 2);

    UndoManager::getInstance().undo();
    const auto& elements = tracks.getChainElements(trackId);
    REQUIRE(elements.size() == 3);
    // Back in the middle, not appended to the end.
    REQUIRE(getDevice(elements[0]).id == first);
    REQUIRE(getDevice(elements[1]).id == middle);
    REQUIRE(getDevice(elements[2]).id == last);

    tracks.clearAllTracks();
}

TEST_CASE("Bypass is applied and reported through the facade", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    const auto path = TrackManager::getInstance().findDevicePath(deviceId);

    TrackManager::getInstance().setDeviceDeltaSoloByPath(path, true);
    REQUIRE(devices.setDeviceBypassed(path, true));
    REQUIRE(devices.getDevice(path)->bypassed);
    REQUIRE_FALSE(devices.getDevice(path)->deltaSolo);

    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE_FALSE(devices.getDevice(path)->bypassed);
    REQUIRE(devices.getDevice(path)->deltaSolo);
    REQUIRE(UndoManager::getInstance().redo());
    REQUIRE(devices.getDevice(path)->bypassed);
    REQUIRE_FALSE(devices.getDevice(path)->deltaSolo);

    REQUIRE(devices.setDeviceBypassed(path, false));
    REQUIRE_FALSE(devices.getDevice(path)->bypassed);

    REQUIRE_FALSE(devices.setDeviceBypassed(ChainNodePath{}, true));

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("The AI opt-in gate names slots", "[device-api][mutation][2638]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Synth");

    // A hosted plugin's own parameters start at slot 2, past the wrapper pair,
    // so a selection of the first one is {2} rather than {0}.
    DeviceInfo device;
    device.name = "Plugin";
    device.pluginId = "com.example.plugin";
    device.format = PluginFormat::VST3;
    for (int slot = 2; slot < 5; ++slot) {
        ParameterInfo info;
        info.valueConvention = ParameterValueConvention::Normalized;
        info.paramIndex = slot;
        info.name = "P" + juce::String(slot);
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.currentValue = 0.0f;
        device.parameters.push_back(info);
    }
    device.aiSoundDesignerParameters = {2};

    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    const auto path = tracks.findDevicePath(deviceId);

    DeviceApiLive devices;

    CHECK(devices.setDeviceParameter(path, 2, 0.5f));

    // Slot 4 sits where the selection's own number would land if this counted
    // positions, and nobody opted it in.
    CHECK_FALSE(devices.setDeviceParameter(path, 4, 0.5f));

    tracks.clearAllTracks();
}

TEST_CASE("Parameter writes are range-checked rather than clamped", "[device-api][mutation]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Synth");

    DeviceInfo device;
    device.name = "Filter";
    device.pluginId = "test_filter";
    // Internal, so the external-plugin opt-in gate stays out of this test's way.
    device.format = PluginFormat::Internal;
    ParameterInfo cutoff;
    cutoff.paramIndex = 0;
    cutoff.name = "Cutoff";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.currentValue = 440.0f;
    device.parameters.push_back(cutoff);
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    const auto path = tracks.findDevicePath(deviceId);

    DeviceApiLive devices;
    REQUIRE(devices.setDeviceParameter(path, 0, 880.0f));

    SECTION("out of range is refused") {
        // Clamping would report success while setting something else.
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, 19.0f));
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, 20001.0f));
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, std::numeric_limits<float>::quiet_NaN()));
    }

    SECTION("unknown parameter index is refused") {
        REQUIRE_FALSE(devices.setDeviceParameter(path, 7, 100.0f));
    }

    SECTION("unknown device is refused") {
        REQUIRE_FALSE(devices.setDeviceParameter(ChainNodePath{}, 0, 100.0f));
    }

    tracks.clearAllTracks();
}

TEST_CASE("External-plugin parameter writes require the user's opt-in", "[device-api][mutation]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Synth");

    DeviceInfo device;
    device.name = "External synth";
    device.pluginId = "external_synth";
    device.format = PluginFormat::VST3;
    for (int i = 0; i < 2; ++i) {
        ParameterInfo param;
        param.valueConvention = ParameterValueConvention::Normalized;
        param.paramIndex = i;
        param.name = "Param " + juce::String(i);
        param.minValue = 0.0f;
        param.maxValue = 1.0f;
        device.parameters.push_back(param);
    }
    // The user opted in only the first parameter under Configure Parameters.
    device.aiSoundDesignerParameters = {0};
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    const auto path = tracks.findDevicePath(deviceId);

    DeviceApiLive devices;

    SECTION("an opted-in parameter accepts the write") {
        REQUIRE(devices.setDeviceParameter(path, 0, 0.5f));
    }

    SECTION("a parameter the user did not opt in is refused") {
        REQUIRE_FALSE(devices.setDeviceParameter(path, 1, 0.5f));
    }

    tracks.clearAllTracks();
}

TEST_CASE("Devices can be added to a chain inside a rack", "[device-api][mutation]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Keys");

    const auto rackId = tracks.addRackToTrack(trackId, "Rack");
    REQUIRE(rackId != INVALID_RACK_ID);
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto* rack = tracks.getRackByPath(rackPath);
    REQUIRE(rack != nullptr);
    REQUIRE_FALSE(rack->chains.empty());
    const auto chainPath = rackPath.withChain(rack->chains.front().id);

    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(chainPath, anyCatalogId(), -1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    // The device is reachable at the path that addresses it inside the chain.
    const auto devicePath = chainPath.withDevice(deviceId);
    REQUIRE(devices.getDevice(devicePath) != nullptr);

    REQUIRE(devices.removeDevice(devicePath));
    REQUIRE(devices.getDevice(devicePath) == nullptr);

    UndoManager::getInstance().undo();
    REQUIRE(devices.getDevice(devicePath) != nullptr);

    tracks.clearAllTracks();
}

TEST_CASE("A Chord Engine is inserted as a listener, whatever the browser said",
          "[device-api][mutation]") {
    // The browser derives a device's type from where it is filed, and the Chord
    // Engine is filed under MIDI. That is the role of a device which produces
    // MIDI, and it produces none (#2427). Corrected on the shared insertion
    // path, so a freshly dropped one is right before the project is ever saved.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Chords");

    DeviceInfo fromBrowser;
    fromBrowser.name = "Chord Engine";
    fromBrowser.pluginId = "midichordengine";
    fromBrowser.format = PluginFormat::Internal;
    fromBrowser.deviceType = DeviceType::MIDI;

    const auto deviceId = tracks.addDeviceToTrack(trackId, fromBrowser);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const auto* device = tracks.getDeviceInChainByPath(tracks.findDevicePath(deviceId));
    REQUIRE(device != nullptr);

    CHECK(device->deviceType == DeviceType::Analysis);
    CHECK(device->consumesMidi());
    CHECK_FALSE(device->emitsMidi());

    tracks.clearAllTracks();
}

TEST_CASE("A Chord Engine dropped into post-FX is a listener too", "[device-api][mutation]") {
    // Post-FX stamps its ids from its own counter and does not go through
    // prepareNewDevice, so the correction the main chain gets had to be made
    // here as well (#2427).
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Chords post-fx");

    DeviceInfo fromBrowser;
    fromBrowser.name = "Chord Engine";
    fromBrowser.pluginId = "midichordengine";
    fromBrowser.format = PluginFormat::Internal;
    fromBrowser.deviceType = DeviceType::MIDI;

    const auto deviceId = tracks.addDeviceToPostFx(trackId, fromBrowser);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const auto* track = tracks.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.postFxChainElements.size() == 1);

    const auto& device = track->chain.postFxChainElements.front().device;
    CHECK(device.deviceType == DeviceType::Analysis);
    CHECK(device.consumesMidi());
    CHECK_FALSE(device.emitsMidi());

    tracks.clearAllTracks();
}

TEST_CASE("Drum Grid facade edits pads and their device chains as undoable steps",
          "[device-api][pads]") {
    auto& tm = TrackManager::getInstance();
    const auto trackId = freshTrack("Remote kit");
    DeviceInfo grid;
    grid.name = "Drum Grid";
    grid.pluginId = "drumgrid";
    grid.isInstrument = true;
    grid.deviceType = DeviceType::Instrument;
    const auto gridId = tm.addDeviceToTrack(trackId, grid);
    REQUIRE(gridId != INVALID_DEVICE_ID);
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    DeviceApiLive devices;
    const auto chainId = devices.createPad(gridPath, 0);
    REQUIRE(chainId != INVALID_CHAIN_ID);
    REQUIRE(tm.getPad(gridPath, 0) != nullptr);
    UndoManager::getInstance().undo();
    REQUIRE(tm.getPad(gridPath, 0) == nullptr);
    UndoManager::getInstance().redo();
    REQUIRE(tm.getPad(gridPath, 0)->id == chainId);

    const auto catalogId = anyCatalogId();
    const auto voiceId = devices.setPadVoice(gridPath, 0, catalogId);
    REQUIRE(voiceId != INVALID_DEVICE_ID);
    const auto chainPath = ChainNodePath::padChain(trackId, gridId, chainId);
    REQUIRE(devices.getDevice(chainPath.withDevice(voiceId)) != nullptr);
    const auto effectId = devices.addDevice(chainPath, catalogId, -1);
    REQUIRE(effectId != INVALID_DEVICE_ID);
    REQUIRE(devices.getDevice(chainPath.withDevice(effectId)) != nullptr);
    REQUIRE(devices.moveDevice(chainPath.withDevice(effectId), 0));
    REQUIRE(magda::getDevice(tm.getPad(gridPath, 0)->elements.front()).id == effectId);
    REQUIRE(devices.removeDevice(chainPath.withDevice(effectId)));
    REQUIRE(devices.getDevice(chainPath.withDevice(effectId)) == nullptr);
    UndoManager::getInstance().undo();
    REQUIRE(devices.getDevice(chainPath.withDevice(effectId)) != nullptr);

    REQUIRE(devices.createPad(gridPath, 1) != INVALID_CHAIN_ID);
    PadUpdate overlap;
    overlap.highNote = padNoteFor(1);
    REQUIRE_FALSE(devices.updatePad(gridPath, 0, overlap));
    REQUIRE(tm.getPad(gridPath, 0)->highNote == padNoteFor(0));

    PadUpdate settings;
    settings.levelDb = -6.0f;
    settings.pan = 0.25f;
    settings.muted = true;
    settings.outputBus = 1;
    REQUIRE(devices.updatePad(gridPath, 0, settings));
    REQUIRE(tm.getPad(gridPath, 0)->volume == -6.0f);
    REQUIRE(tm.getPad(gridPath, 0)->outputIndex == 1);
    UndoManager::getInstance().undo();
    REQUIRE(tm.getPad(gridPath, 0)->volume == 0.0f);
    REQUIRE(tm.getPad(gridPath, 0)->outputIndex == 0);

    PadUpdate moved;
    moved.lowNote = padNoteFor(2);
    moved.highNote = padNoteFor(2);
    moved.levelDb = -9.0f;
    REQUIRE(devices.updatePad(gridPath, 0, moved));
    REQUIRE(tm.getPad(gridPath, 0) == nullptr);
    REQUIRE(tm.getPad(gridPath, 2)->id == chainId);
    REQUIRE(tm.getPad(gridPath, 2)->volume == -9.0f);

    const auto sampleFile = juce::File::createTempFile(".wav");
    REQUIRE(sampleFile.create().wasOk());
    const unsigned char wav[] = {'R', 'I', 'F', 'F', 38,  0,  0, 0, 'W', 'A', 'V', 'E',
                                 'f', 'm', 't', ' ', 16,  0,  0, 0, 1,   0,   1,   0,
                                 68,  172, 0,   0,   136, 88, 1, 0, 2,   0,   16,  0,
                                 'd', 'a', 't', 'a', 2,   0,  0, 0, 0,   0};
    REQUIRE(sampleFile.appendData(wav, sizeof(wav)));
    const auto sampleId = devices.setPadSample(gridPath, 3, sampleFile.getFullPathName());
    REQUIRE(sampleId != INVALID_DEVICE_ID);
    REQUIRE(devices.getDevice(ChainNodePath::padChain(trackId, gridId, tm.getPad(gridPath, 3)->id)
                                  .withDevice(sampleId)) != nullptr);
    REQUIRE(sampleFile.deleteFile());
    REQUIRE(devices.setPadSample(gridPath, 4, sampleFile.getFullPathName()) == INVALID_DEVICE_ID);
    REQUIRE(tm.getPad(gridPath, 4) == nullptr);

    tm.clearAllTracks();
}
