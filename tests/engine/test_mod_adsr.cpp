#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModBridge.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_mod_adsr.cpp
 * @brief The envelope generator (#2120).
 *
 * Three claims, separable the way the LFO's are. The shape is fixed: a
 * segment's curvature is a persisted number and what it draws has to stay the
 * same curve. The run is the engine's: which stage the envelope
 * is in after a block, what a gate edge does from each of them, and what a
 * tempo-synced stage is worth in seconds. And the wiring is the table's: that
 * the model's seven fields reach the block and that the value comes out of a
 * device the far end.
 */

using namespace magda;
using magda::engine::AdsrSettings;
using magda::engine::AdsrStage;
using magda::engine::AdsrState;
using magda::engine::advanceAdsr;
using magda::engine::BlockInfo;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::ModContribution;
using magda::engine::ModKind;
using magda::engine::ModRuntime;
using magda::engine::ModSync;
using magda::engine::ModTiming;
using magda::engine::modTimingFor;
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

ModTiming timing(double bpm = 120.0, int numerator = 4, int denominator = 4) {
    return ModTiming{kSampleRate, bpm, numerator, denominator};
}

/// A block of @p numSamples with the transport rolling, which is what a
/// transport-gated envelope needs and what a free-running one ignores.
BlockInfo rollingBlock(int numSamples, bool playing = true) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = playing;
    return block;
}

/// A block a millisecond long at the test's sample rate, which makes a stage
/// length in milliseconds a block count.
BlockInfo millisecondBlock(bool playing = true) {
    return rollingBlock(static_cast<int>(kSampleRate / 1000.0), playing);
}

float step(AdsrState& state, const AdsrSettings& settings, const BlockInfo& block,
           const ModTiming& time = timing()) {
    return advanceAdsr(state, settings, modBlockFor(block, time), time);
}

/// Run @p count blocks and return the last value.
float run(AdsrState& state, const AdsrSettings& settings, int count,
          const BlockInfo& block = millisecondBlock()) {
    float value = 0.0f;
    for (int i = 0; i < count; ++i)
        value = step(state, settings, block);
    return value;
}

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeDevice(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.mods = createDefaultMods(0);

    ParameterInfo info(0, "Level", "", 0.0f, 1.0f, 0.0f);
    info.currentValue = 0.0f;
    device.parameters.push_back(info);

    return device;
}

/// A track with one device and one envelope wired to that device's parameter.
TrackInfo trackWithEnvelope(LFOTriggerMode trigger = LFOTriggerMode::Free, bool running = true) {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7)));
    track.mods = createDefaultMods(1);
    track.mods[0].type = ModType::Envelope;
    track.mods[0].triggerMode = trigger;

    // Whether the model says the envelope is already open. A note-triggered one
    // that is not running starts shut, which is what waiting for a note is.
    track.mods[0].running = running;
    track.mods[0].envAttackMs = 10.0f;
    track.mods[0].envDecayMs = 10.0f;
    track.mods[0].envSustain = 0.5f;
    track.mods[0].envReleaseMs = 10.0f;
    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false, true});
    return track;
}

ParamKey deviceParam(TrackId track, DeviceId device, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = track;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, device};
    key.index = index;
    return key;
}

ParamTable tableFor(const std::vector<TrackInfo>& tracks) {
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master, {});
}

struct Harness {
    explicit Harness(const ParamTable& table)
        : links(static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1))),
          segments(
              static_cast<std::size_t>(magda::engine::ResolvedParams::kDefaultSegmentCapacity)) {
        values.prepare(table.size());
        mods.prepare(table, magda::engine::RenderContext{kSampleRate, 512, 2});
    }

    void run(const ParamTable& table, const BlockInfo& block) {
        resolveParams(table, values, links, segments, block, &mods);
    }

    ResolvedParams values;
    ModRuntime mods;
    std::vector<ModContribution> links;
    std::vector<ParamSegment> segments;
};

}  // namespace

// =============================================================================
// The shape
// =============================================================================

// =============================================================================
// The run
// =============================================================================

TEST_CASE("A synced stage lasts the map's beats across a tempo step", "[engine][mod][adsr][2340]") {
    // 120 held to beat 2, then 60, and a block from beat 1.5 to 2.5: half a
    // beat either side of the step, a quarter of a second at 120 and half a
    // second at 60, three quarters of a second in all.
    //
    // A stage of one half note is two beats and the block covered one of them,
    // so the envelope is halfway up the attack. Three quarters of a second
    // against a stage the opening tempo makes a second long would have put it
    // three quarters of the way up (#2340).
    const magda::engine::TempoMap tempo({{.startBeat = 0.0, .bpm = 120.0},
                                         {.startBeat = 2.0, .bpm = 120.0},
                                         {.startBeat = 2.0, .bpm = 60.0}},
                                        {{.startBeat = 0.0, .numerator = 4, .denominator = 4}});

    BlockInfo block;
    block.playing = true;
    block.beats.start = 1.5;
    block.beats.end = 2.5;
    block.seconds.start = tempo.beatToTime(block.beats.start);
    block.seconds.end = tempo.beatToTime(block.beats.end);
    block.numSamples =
        static_cast<int>(std::llround((block.seconds.end - block.seconds.start) * kSampleRate));
    block.sampleRate = kSampleRate;
    block.tempo = &tempo;

    AdsrSettings settings;
    settings.sync = ModSync::Free;
    settings.tempoSync = true;
    settings.rateType = static_cast<int>(ModRateType::Half);

    AdsrState state;

    CHECK(step(state, settings, block, modTimingFor(block, kSampleRate)) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Attack);
}

TEST_CASE("A synced attack runs in bars, not the opening signature's beats",
          "[engine][mod][adsr][2340]") {
    // The LFO's claim, on the envelope. 120 throughout, four four to beat 2 and
    // three four from it: a block from beat 1 to beat 3 covers one beat of the
    // four-beat bar and one beat of the three-beat bar that follows, a quarter
    // plus a third. A one-bar attack is that far up, linearly from zero; the
    // opening signature's formula would divide the whole two beats by four and
    // say a half (#2340).
    const magda::engine::TempoMap tempo({{.startBeat = 0.0, .bpm = 120.0}},
                                        {{.startBeat = 0.0, .numerator = 4, .denominator = 4},
                                         {.startBeat = 2.0, .numerator = 3, .denominator = 4}});

    BlockInfo block;
    block.playing = true;
    block.beats.start = 1.0;
    block.beats.end = 3.0;
    block.seconds.start = tempo.beatToTime(block.beats.start);
    block.seconds.end = tempo.beatToTime(block.beats.end);
    block.numSamples =
        static_cast<int>(std::llround((block.seconds.end - block.seconds.start) * kSampleRate));
    block.sampleRate = kSampleRate;
    block.tempo = &tempo;

    AdsrSettings settings;
    settings.sync = ModSync::Free;
    settings.tempoSync = true;
    settings.rateType = static_cast<int>(ModRateType::Bar);

    AdsrState state;

    const auto expected = static_cast<float>(0.25 + 1.0 / 3.0);
    CHECK(step(state, settings, block, modTimingFor(block, kSampleRate)) == approx(expected));
    CHECK(state.stage == AdsrStage::Attack);
}

// =============================================================================
// The wiring
// =============================================================================

TEST_CASE("An envelope's settings reach the table", "[engine][mod][adsr][table]") {
    auto track = trackWithEnvelope();
    track.mods[0].envAttackMs = 12.5f;
    track.mods[0].envDecayMs = 33.0f;
    track.mods[0].envSustain = 0.25f;
    track.mods[0].envReleaseMs = 400.0f;
    track.mods[0].envAttackCurve = 0.4f;
    track.mods[0].envDecayCurve = -0.2f;
    track.mods[0].envReleaseCurve = 0.1f;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    const auto& modifier = table.modifiers.front();
    CHECK(modifier.kind == ModKind::Adsr);
    CHECK(modifier.adsr.attackMs == approx(12.5f));
    CHECK(modifier.adsr.decayMs == approx(33.0f));
    CHECK(modifier.adsr.sustain == approx(0.25f));
    CHECK(modifier.adsr.releaseMs == approx(400.0f));
    CHECK(modifier.adsr.attackCurve == approx(0.4f));
    CHECK(modifier.adsr.decayCurve == approx(-0.2f));
    CHECK(modifier.adsr.releaseCurve == approx(0.1f));
}

TEST_CASE("Tempo sync does not fold into an envelope's gate", "[engine][mod][adsr][table]") {
    // The one place the envelope's fold differs from the LFO's. For an LFO,
    // tempo sync decides whether the phase is a function of the timeline; for
    // an envelope it only scales the stages, so a synced free-running envelope
    // is still free running.
    auto track = trackWithEnvelope(LFOTriggerMode::Free);
    track.mods[0].tempoSync = true;
    track.mods[0].syncDivision = SyncDivision::Quarter;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    CHECK(table.modifiers.front().adsr.sync == ModSync::Free);
    CHECK(table.modifiers.front().adsr.tempoSync);
    CHECK(table.modifiers.front().adsr.rateType ==
          magda::syncDivisionToTeRateOrdinal(SyncDivision::Quarter));

    // The LFO on the same fields would have folded to a timeline-locked run.
    auto asLfo = track;
    asLfo.mods[0].type = ModType::LFO;
    CHECK(tableFor({asLfo}).modifiers.front().lfo.sync == ModSync::Transport);
}

TEST_CASE("An envelope drives a device parameter through the block",
          "[engine][mod][adsr][runtime]") {
    const auto table = tableFor({trackWithEnvelope(LFOTriggerMode::Free)});
    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != magda::engine::INVALID_PARAM_ID);

    Harness harness(table);
    const auto block = millisecondBlock();

    // A free-running envelope needs nothing to start it, so the parameter is
    // already climbing on the first block.
    harness.run(table, block);
    CHECK(harness.values[param].value() == approx(0.1f));

    for (int i = 0; i < 9; ++i)
        harness.run(table, block);
    CHECK(harness.values[param].value() == approx(1.0f));
}

TEST_CASE("A note opens an envelope's gate and the last note lifting shuts it",
          "[engine][mod][adsr][runtime]") {
    const auto table = tableFor({trackWithEnvelope(LFOTriggerMode::MIDI, false)});
    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != magda::engine::INVALID_PARAM_ID);
    REQUIRE(table.modifiers.size() == 1);

    Harness harness(table);
    const auto block = millisecondBlock();

    const auto runBlocks = [&](int count) {
        for (int i = 0; i < count; ++i)
            harness.run(table, block);
        return harness.values[param].value();
    };

    // Waiting: a note-triggered envelope the model does not call running is
    // shut before its first note, and a shut envelope contributes nothing.
    CHECK(runBlocks(1) == approx(0.0f));

    // Through the attack and the decay to the sustain, and held there.
    harness.mods.noteOn(0, table);
    CHECK(runBlocks(25) == approx(0.5f));

    // A second note retriggers, which is what a note does to an envelope, and
    // is also a second note held.
    harness.mods.noteOn(0, table);
    CHECK(runBlocks(25) == approx(0.5f));

    // The first of the two lifting leaves the gate open: an envelope that
    // released on the first note off would cut a chord short.
    harness.mods.noteOff(0, table);
    CHECK(runBlocks(20) == approx(0.5f));

    // The last one shuts it, and the release runs from where the envelope was.
    harness.mods.noteOff(0, table);
    CHECK(runBlocks(5) == approx(0.25f));
    CHECK(runBlocks(5) == approx(0.0f));
}

TEST_CASE("A cross-track envelope hears its source and nothing else",
          "[engine][mod][adsr][runtime]") {
    // The rack the envelope lives on is sidechained from track 2, so the notes
    // track 1 happens to be playing are not its notes.
    auto source = makeTrack(2);

    RackInfo rack;
    rack.id = 4;
    rack.sidechain.type = SidechainConfig::Type::MIDI;
    rack.sidechain.sourceTrackId = 2;
    rack.mods = createDefaultMods(1);
    rack.mods[0].type = ModType::Envelope;
    rack.mods[0].triggerMode = LFOTriggerMode::MIDI;
    rack.mods[0].envAttackMs = 10.0f;
    rack.mods[0].envDecayMs = 10.0f;
    rack.mods[0].envSustain = 0.5f;
    rack.mods[0].envReleaseMs = 10.0f;
    rack.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::chainDevice(1, 4, 10, 7), 0), 1.0f, false, true});

    ChainInfo chain;
    chain.id = 10;
    chain.elements.push_back(makeDeviceElement(makeDevice(7)));
    rack.chains.push_back(std::move(chain));

    auto destination = makeTrack(1);
    destination.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto table = tableFor({destination, source});
    REQUIRE(table.modifiers.size() == 1);

    // It listens to the source, which is what the plan's tap is emitted for.
    CHECK(table.modifiers.front().source == 2);
    CHECK(table.modifiers.front().adsr.skipNativeResync);

    Harness harness(table);
    const auto block = millisecondBlock();

    // A note from where it lives is refused, gate and phase alike.
    harness.mods.noteOn(0, table);
    for (int i = 0; i < 10; ++i)
        harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(0.0f));

    // The source's own trigger is the one it exists to follow.
    harness.mods.trigger(0, table);
    harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(0.0f));  // the gap the trigger asked for

    for (int i = 0; i < 10; ++i)
        harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(1.0f));
}
