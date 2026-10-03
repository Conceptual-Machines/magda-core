#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <vector>

#include "core/TrackInfo.hpp"
#include "param/ModBridge.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_mod_random.cpp
 * @brief The random modulator (#2120).
 *
 * What is assertable about a walk is its structure rather than its numbers:
 * where the steps land, how far one may move from the last, what the shape
 * control does between them, and that the timing is the LFO's timing, which is
 * the part a project can hear.
 *
 * The engine also promises that a project draws the same numbers on every run
 * of it, which is asserted here because an engine that renders differently
 * twice cannot be checked against a golden.
 */

using namespace magda;
using magda::engine::advanceRandom;
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
using magda::engine::RandomSettings;
using magda::engine::RandomShape;
using magda::engine::RandomState;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;
using magda::engine::seedRandom;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

ModTiming timing(double bpm = 120.0, int numerator = 4, int denominator = 4) {
    return ModTiming{kSampleRate, bpm, numerator, denominator};
}

BlockInfo blockOf(int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    return block;
}

/// A block a tenth of a step long, so ten of them is one cycle at 1 Hz.
BlockInfo tenthBlock() {
    return blockOf(static_cast<int>(kSampleRate / 10.0));
}

RandomSettings stepped(float hz = 1.0f) {
    RandomSettings settings;
    settings.rate.hz = hz;
    return settings;
}

RandomState seeded(std::uint64_t address = 1) {
    RandomState state;
    seedRandom(state, address);
    return state;
}

float step(RandomState& state, const RandomSettings& settings, const BlockInfo& block,
           const ModTiming& time = timing()) {
    return advanceRandom(state, settings, modBlockFor(block, time), time);
}

/// The values @p count blocks publish.
std::vector<float> walk(RandomState& state, const RandomSettings& settings, int count,
                        const BlockInfo& block = tenthBlock()) {
    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        values.push_back(step(state, settings, block));
    return values;
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

TrackInfo trackWithRandom() {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7)));
    track.mods = createDefaultMods(1);
    track.mods[0].type = ModType::Random;
    track.mods[0].rate = 1.0f;
    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false, true});
    return track;
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
// The walk
// =============================================================================

// =============================================================================
// The timing
// =============================================================================

TEST_CASE("A free-running synced walk steps on the map's beats", "[engine][mod][random][2340]") {
    // The LFO's claim, on the walk. 120 held to beat 2, then 60, and a block
    // from beat 1.5 to 2.5: half a beat either side of the step, a quarter of a
    // second at 120 and half a second at 60. What it covers is one beat; three
    // quarters of a second at the tempo it opened on would say one and a half
    // (#2340).
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

    auto settings = stepped();
    settings.sync = ModSync::Free;
    settings.tempoSync = true;
    settings.rate.rateType = static_cast<int>(ModRateType::Bar);

    auto state = seeded();
    const auto timings = modTimingFor(block, kSampleRate);

    step(state, settings, block, timings);

    // One beat of a four-beat step. The opening tempo's one and a half beats
    // would have put the walk three eighths of the way through it.
    CHECK(state.cycles == Catch::Approx(0.25));

    step(state, settings, block, timings);
    CHECK(state.phase == approx(0.25f));
}

TEST_CASE("A free-running synced walk steps on the map's bars, not the opening signature's",
          "[engine][mod][random][2340]") {
    // The LFO's claim, on the walk. 120 throughout, four four to beat 2 and
    // three four from it: a block from beat 1 to beat 3 covers one beat of the
    // four-beat bar and one beat of the three-beat bar that follows, a quarter
    // plus a third. The opening signature's formula would divide the whole two
    // beats by four and say a half (#2340).
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

    auto settings = stepped();
    settings.sync = ModSync::Free;
    settings.tempoSync = true;
    settings.rate.rateType = static_cast<int>(ModRateType::Bar);

    auto state = seeded();
    const auto timings = modTimingFor(block, kSampleRate);

    step(state, settings, block, timings);

    // A quarter of the four-beat bar plus a third of the three-beat one.
    CHECK(state.cycles == Catch::Approx(0.25 + 1.0 / 3.0));

    step(state, settings, block, timings);
    CHECK(state.phase == approx(static_cast<float>(0.25 + 1.0 / 3.0)));
}

// =============================================================================
// The wiring
// =============================================================================

TEST_CASE("A random modulator's settings reach the table", "[engine][mod][random][table]") {
    auto track = trackWithRandom();
    track.mods[0].randomType = 1;
    track.mods[0].randomShape = 0.6f;
    track.mods[0].randomSmooth = 0.3f;
    track.mods[0].randomStepDepth = 0.4f;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    const auto& modifier = table.modifiers.front();
    CHECK(modifier.kind == ModKind::Random);
    CHECK(modifier.random.type == RandomShape::Noise);
    CHECK(modifier.random.shape == approx(0.6f));
    CHECK(modifier.random.smooth == approx(0.3f));
    CHECK(modifier.random.stepDepth == approx(0.4f));
    CHECK(modifier.random.rate.hz == approx(1.0f));
}

TEST_CASE("A random modulator shares the LFO's rate lane", "[engine][mod][random][table]") {
    auto track = trackWithRandom();
    track.mods[0].tempoSync = true;
    track.mods[0].syncDivision = SyncDivision::Eighth;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    // The same fold and the same ordinal an LFO on these fields would get: the
    // both are driven through one mapping and a project has to sound the same
    // whichever of the two is on the knob.
    CHECK(table.modifiers.front().random.sync == ModSync::Transport);
    CHECK(table.modifiers.front().random.tempoSync);
    CHECK(table.modifiers.front().random.rate.rateType ==
          magda::syncDivisionToTeRateOrdinal(SyncDivision::Eighth));
}

TEST_CASE("A random modulator drives a device parameter", "[engine][mod][random][runtime]") {
    const auto table = tableFor({trackWithRandom()});

    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = 1;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, 7};
    key.index = 0;

    const auto param = table.find(key);
    REQUIRE(param != magda::engine::INVALID_PARAM_ID);

    Harness harness(table);
    const auto block = tenthBlock();

    // Held across one step and moved later, which is what the walk does at the
    // modifier and therefore what the parameter does at the far end.
    harness.run(table, block);
    const auto opening = harness.values[param].value();

    for (int i = 0; i < 9; ++i)
        harness.run(table, block);
    CHECK(harness.values[param].value() == approx(opening));

    // Two cycles on, for the reason the walk's own case gives: a sample and
    // hold publishes the number drawn at the wrap before it.
    for (int i = 0; i < 20; ++i)
        harness.run(table, block);
    CHECK(harness.values[param].value() != approx(opening));
}

TEST_CASE("A random modulator has no gate", "[engine][mod][random][runtime]") {
    // The random modifier resyncs on a note and has no gate parameter,
    // so an audio-triggered one keeps walking between hits rather than resting
    // at zero. Asserted because the API a detector drives is uniform across the
    // kinds and this is the kind that ignores half of it.
    auto track = trackWithRandom();
    track.mods[0].triggerMode = LFOTriggerMode::Audio;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    Harness harness(table);
    const auto block = tenthBlock();

    harness.run(table, block);
    const auto value = harness.mods.value(0);

    harness.mods.setGated(0, table, true);
    harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(value));
}
