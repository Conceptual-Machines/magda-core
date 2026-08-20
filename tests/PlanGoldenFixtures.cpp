#include "PlanGoldenFixtures.hpp"

#include <memory>
#include <utility>

#include "core/RackInfo.hpp"

/**
 * @file PlanGoldenFixtures.cpp
 * @brief The projects the plan compiler is pinned against (#2076).
 *
 * Each fixture names one decision the compiler makes and builds the smallest
 * project that forces it. Small on purpose: a golden's whole value is that a
 * human can read the diff and say whether the change was meant, and a fixture
 * with four tracks of everything produces a diff nobody reads.
 *
 * They are model values only. Nothing here compiles, dumps or compares.
 */

namespace magda::goldens {
namespace {

TrackInfo track(TrackId id, TrackType type = TrackType::Audio) {
    TrackInfo value;
    value.id = id;
    value.type = type;
    value.name = "Track " + juce::String(id);
    value.audioOutputDevice = "master";
    return value;
}

TrackInfo master() {
    auto value = track(MASTER_TRACK_ID, TrackType::Master);
    value.audioOutputDevice = {};
    return value;
}

DeviceInfo effect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    return device;
}

/// An effect with parameters, for the fixture that pins the parameter table
/// rather than the graph. Ranges that read 0 to 100 so a normalised position
/// and the value a device is handed are not the same number in the golden.
DeviceInfo effectWithParameters(DeviceId id, int numParameters) {
    auto device = effect(id);
    for (int index = 0; index < numParameters; ++index) {
        ParameterInfo info(index, "P" + juce::String(index), "%", 0.0f, 100.0f, 0.0f);
        info.currentValue = 25.0f * static_cast<float>(index);
        device.parameters.push_back(info);
    }
    return device;
}

DeviceInfo instrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    return device;
}

/// Where a device sits, as the key its process op will carry. The runner
/// requires it to match one op exactly, so a fixture cannot name a location
/// that does not exist and get silence instead of a failure.
engine::OpKey deviceAt(TrackId trackId, DeviceId deviceId, RackId rackId = INVALID_RACK_ID,
                       ChainId chainId = INVALID_CHAIN_ID,
                       ChainSegment segment = ChainSegment::Fx) {
    engine::OpKey key;
    key.trackId = trackId;
    key.rackId = rackId;
    key.chainId = chainId;
    key.deviceId = deviceId;
    key.role = engine::OpRole::DeviceProcess;
    key.segment = segment;
    return key;
}

/// Device meters off unless a fixture is about them: they are three ops per
/// slot and they bury everything else in the dump.
engine::CompileOptions withoutMeters() {
    engine::CompileOptions options;
    options.deviceMeters = false;
    return options;
}

// --- the fixtures ------------------------------------------------------------

Fixture bareTrack() {
    Fixture value;
    value.name = "bare-track";
    value.covers = "one audio track into master: the smallest whole plan there is";
    value.tracks = {track(1)};
    value.master = master();
    value.options = withoutMeters();
    return value;
}

Fixture deviceChain() {
    Fixture value;
    value.name = "device-chain";
    value.covers = "chain order, and the three ops a device slot compiles to";
    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(8)));
    value.master = master();
    return value;
}

Fixture instrumentTrack() {
    Fixture value;
    value.name = "instrument-track";
    value.covers = "a MIDI clip reaching an instrument, and the audio leaving it";
    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(instrument(3)));
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.master = master();
    value.options = withoutMeters();
    return value;
}

Fixture fanIn() {
    Fixture value;
    value.name = "fan-in";
    value.covers = "two tracks meeting at master: where the delays go, and in what order";
    value.tracks = {track(1), track(2)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.master = master();
    value.options = withoutMeters();

    // The unequal path the delays exist for. Only the device on track 1
    // reports latency, so track 2 is the one that has to be held back.
    value.deviceLatency = {{deviceAt(1, 7), 64}};
    return value;
}

Fixture cascadedLatency() {
    Fixture value;
    value.name = "cascaded-latency";
    value.covers = "latency accumulating along a chain, and a send arriving carrying it";
    value.tracks = {track(1), track(2, TrackType::Aux)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(8)));
    value.tracks[0].sends.push_back(SendInfo{0, 0.5f, false, 2});
    value.tracks[1].auxBusIndex = 0;
    value.tracks[1].chain.fxChainElements.push_back(makeDeviceElement(effect(9)));
    value.master = master();
    value.options = withoutMeters();

    // Three different numbers, so a dump that summed them in the wrong order
    // would not land on the right total by luck.
    value.deviceLatency = {{deviceAt(1, 7), 32}, {deviceAt(1, 8), 128}, {deviceAt(2, 9), 16}};
    return value;
}

Fixture rackChains() {
    Fixture value;
    value.name = "rack-chains";
    value.covers = "two parallel chains, their mix, and the fader over it";

    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    for (const ChainId chainId : {ChainId{10}, ChainId{11}}) {
        ChainInfo chain;
        chain.id = chainId;
        chain.elements.push_back(makeDeviceElement(effect(static_cast<DeviceId>(chainId))));
        rack->chains.push_back(std::move(chain));
    }

    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(ChainElement{std::move(rack)});
    value.master = master();
    value.options = withoutMeters();

    // Unequal chains, so the rack's own mix has something to align.
    value.deviceLatency = {{deviceAt(1, 10, 5, 10), 48}};
    return value;
}

Fixture sidechain() {
    Fixture value;
    value.name = "sidechain";
    value.covers = "a device's key input, taken from another track";

    auto compressor = effect(7);
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    value.tracks = {track(1), track(2)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));
    value.master = master();
    value.options = withoutMeters();
    return value;
}

Fixture sectionLocalDeviceIds() {
    Fixture value;
    value.name = "section-local-device-ids";
    value.covers = "the same device id in FX, post-FX and mixer analysis stays three identities";
    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(3)));
    value.tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{effect(3)});

    auto analysis = effect(3);
    analysis.deviceType = DeviceType::Analysis;
    value.tracks[0].chain.mixerAnalysisElements.push_back(PostFxChainElement{analysis});

    value.master = master();
    value.options = withoutMeters();
    value.deviceLatency = {
        {deviceAt(1, 3), 16},
        {deviceAt(1, 3, INVALID_RACK_ID, INVALID_CHAIN_ID, ChainSegment::PostFx), 32},
        {deviceAt(1, 3, INVALID_RACK_ID, INVALID_CHAIN_ID, ChainSegment::MixerAnalysis), 64},
    };
    return value;
}

/// The parameter table's own fixture (#2117). Everything the plan fixtures do
/// not have: parameters with scales, a macro driving one, a modifier driving
/// another, and a macro driving a macro, which is what the resolution order is
/// for.
Fixture parameterLinks() {
    Fixture value;
    value.name = "parameter-links";
    value.covers = "macros, modifiers and the order a chain of them resolves in";

    auto device = effectWithParameters(7, 3);
    device.macros = createDefaultMacros(1);
    device.macros[0].value = 0.25f;
    device.macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 2), 0.5f, true});

    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(device));

    value.tracks[0].macros = createDefaultMacros(2);
    value.tracks[0].macros[0].value = 1.0f;
    value.tracks[0].macros[0].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 1), 0.5f, false});
    value.tracks[0].macros[1].value = 0.5f;
    value.tracks[0].macros[1].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false});

    // A tempo-synced LFO with a drawn cycle, driving a device parameter, with
    // its own rate driven by a macro (#2119). The rate link is what puts the
    // modifier inside the resolution order rather than beside it.
    value.tracks[0].mods = createDefaultMods(1);
    value.tracks[0].mods[0].value = 0.75f;
    value.tracks[0].mods[0].waveform = LFOWaveform::Custom;
    value.tracks[0].mods[0].tempoSync = true;
    value.tracks[0].mods[0].syncDivision = SyncDivision::Half;
    value.tracks[0].mods[0].phaseOffset = 0.25f;
    value.tracks[0].mods[0].curvePoints = {{0.0f, 0.0f}, {0.5f, 1.0f}};
    value.tracks[0].mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 1), 0.5f, true, true});

    value.tracks[0].macros[1].links.push_back(
        MacroLink{ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0), 0.25f, false});

    // A lane over the device's first parameter, and one over the track fader,
    // which is a parameter only because the lane reaches it (#2118).
    AutomationLaneInfo cutoff;
    cutoff.id = 1;
    cutoff.target = ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0);
    cutoff.authorityState = AutomationAuthorityState::Reading;
    cutoff.absolutePoints = {{1, 0.0, 0.0}, {2, 4.0, 1.0}};

    AutomationLaneInfo fader;
    fader.id = 2;
    fader.target = ControlTarget::trackVolume(1);
    fader.authorityState = AutomationAuthorityState::Reading;
    fader.absolutePoints = {{3, 0.0, 0.75}, {4, 8.0, 0.0}};

    value.lanes = {cutoff, fader};

    value.master = master();
    value.options = withoutMeters();
    return value;
}

Fixture groupRouting() {
    Fixture value;
    value.name = "group-routing";
    value.covers = "a track routed to a group rather than to master";
    value.tracks = {track(1), track(2, TrackType::Group)};
    value.tracks[0].audioOutputDevice = "track:2";
    value.tracks[1].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.master = master();
    value.options = withoutMeters();
    return value;
}

/// An edit that keeps everything it can. The differ's job is to carry every op
/// the edit did not touch, so the fixture that pins it has to have plenty for
/// it to get wrong: a device inserted ahead of others moves every index behind
/// it, and an index-keyed differ would report them all as new.
Fixture insertDevice() {
    Fixture value;
    value.name = "edit-insert-device";
    value.covers = "op identity across an insert: what carries when indices all move";
    value.tracks = {track(1), track(2)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.tracks[1].chain.fxChainElements.push_back(makeDeviceElement(effect(8)));
    value.master = master();
    value.options = withoutMeters();

    value.editedTracks = value.tracks;
    value.editedTracks[0].chain.fxChainElements.insert(
        value.editedTracks[0].chain.fxChainElements.begin(), makeDeviceElement(effect(9)));
    return value;
}

/// An edit that retires something. A removed device's ops have nowhere to
/// carry to, and the track behind it re-routes onto what is left.
Fixture removeDevice() {
    Fixture value;
    value.name = "edit-remove-device";
    value.covers = "op identity across a removal: what retires, and what re-routes";
    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(8)));
    value.master = master();
    value.options = withoutMeters();

    value.editedTracks = value.tracks;
    value.editedTracks[0].chain.fxChainElements.erase(
        value.editedTracks[0].chain.fxChainElements.begin());
    return value;
}

/// An edit that moves an audio edge, which is what the crossfade pass exists
/// for (#2019). Reordering two devices moves what feeds what without adding or
/// removing anything, so every fade in the golden is a fade the pass chose.
Fixture reorderDevices() {
    Fixture value;
    value.name = "edit-reorder-devices";
    value.covers = "the fades a moved edge earns, and the ops they become";
    value.tracks = {track(1)};
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(7)));
    value.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect(8)));
    value.master = master();
    value.options = withoutMeters();

    value.editedTracks = value.tracks;
    std::swap(value.editedTracks[0].chain.fxChainElements[0],
              value.editedTracks[0].chain.fxChainElements[1]);
    return value;
}

}  // namespace

std::vector<Fixture> planFixtures() {
    std::vector<Fixture> fixtures;
    fixtures.push_back(bareTrack());
    fixtures.push_back(deviceChain());
    fixtures.push_back(instrumentTrack());
    fixtures.push_back(fanIn());
    fixtures.push_back(cascadedLatency());
    fixtures.push_back(rackChains());
    fixtures.push_back(sidechain());
    fixtures.push_back(sectionLocalDeviceIds());
    fixtures.push_back(groupRouting());
    fixtures.push_back(parameterLinks());
    fixtures.push_back(insertDevice());
    fixtures.push_back(removeDevice());
    fixtures.push_back(reorderDevices());
    return fixtures;
}

}  // namespace magda::goldens
