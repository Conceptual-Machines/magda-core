#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <vector>

#include "core/TrackInfo.hpp"
#include "param/ModRandom.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_mod_random.cpp
 * @brief The random modulator (#2120).
 *
 * What is assertable about a walk is its structure rather than its numbers. The
 * fork seeds from the clock and promises neither reproducibility nor
 * independence between two modulators, so a parity case here cannot compare
 * sequences; what it can compare is where the steps land, how far one may move
 * from the last, what the shape control does between them, and that the timing
 * is the LFO's timing, which is the part a project can hear.
 *
 * The one thing this engine does promise and the fork does not is that a
 * project draws the same numbers on every run of it, which is asserted here
 * because an engine that renders differently twice cannot be null-diffed
 * against anything, itself included.
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
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamTable;
using magda::engine::RandomSettings;
using magda::engine::RandomShape;
using magda::engine::RandomState;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;
using magda::engine::restartRandom;
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
    return advanceRandom(state, settings, block, time);
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

TEST_CASE("A stepped walk changes value once per cycle", "[engine][mod][random]") {
    auto settings = stepped();
    auto state = seeded();

    // Held across the step, because a shape of zero is a sample and hold: ten
    // blocks of a tenth each is one cycle, and the value inside it is one value.
    const auto first = walk(state, settings, 10);
    const auto second = walk(state, settings, 10);
    const auto third = walk(state, settings, 10);

    for (const auto& cycle : {first, second, third})
        for (const auto value : cycle)
            CHECK(value == approx(cycle.front()));

    // And a different one two cycles on. Two rather than one, because a held
    // step publishes the value drawn at the wrap before it: the fork reads the
    // shape control as "how much of the step is spent travelling", and a step
    // that travels for none of itself is a sample and hold one step behind.
    CHECK(third.front() != approx(first.front()));
}

TEST_CASE("Step depth bounds how far one step moves", "[engine][mod][random]") {
    auto settings = stepped();
    settings.stepDepth = 0.2f;

    auto state = seeded(7);
    float previous = walk(state, settings, 10).front();

    // Half the control each way, clipped to the range, which is the fork's
    // arithmetic for a unipolar modulator. A hundred steps is enough for a
    // breach to show if the bound were wrong.
    for (int i = 0; i < 100; ++i) {
        const auto value = walk(state, settings, 10).front();
        CHECK(std::abs(value - previous) <= 0.1f + 1e-4f);
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
        previous = value;
    }
}

TEST_CASE("A full step depth still stays in range", "[engine][mod][random]") {
    auto settings = stepped();
    settings.stepDepth = 1.0f;

    auto state = seeded(11);
    for (int i = 0; i < 200; ++i) {
        const auto value = walk(state, settings, 10).front();
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("Shape decides how much of a step is spent travelling", "[engine][mod][random]") {
    SECTION("a shape of one ramps the whole way across") {
        auto settings = stepped();
        settings.shape = 1.0f;

        auto state = seeded(3);

        // Past the first wrap, so there is a previous value to travel from.
        walk(state, settings, 10);
        const auto values = walk(state, settings, 10);

        // Monotone across the step, in whichever direction this step went.
        const bool rising = values.back() > values.front();
        for (std::size_t i = 1; i < values.size(); ++i)
            CHECK((rising ? values[i] >= values[i - 1] : values[i] <= values[i - 1]));

        // And it actually moved, rather than being flat in both directions.
        CHECK(values.front() != approx(values.back()));
    }

    SECTION("a shape of zero holds") {
        auto settings = stepped();
        auto state = seeded(3);

        walk(state, settings, 10);
        const auto values = walk(state, settings, 10);
        for (const auto value : values)
            CHECK(value == approx(values.front()));
    }
}

TEST_CASE("Smoothing bends the phase and leaves a held step alone", "[engine][mod][random]") {
    // Smoothing acts on the phase, so it eases the ends of a ramp and does
    // nothing at all to a value that is not travelling.
    auto held = stepped();
    held.smooth = 1.0f;

    auto state = seeded(5);
    walk(state, held, 10);
    const auto values = walk(state, held, 10);
    for (const auto value : values)
        CHECK(value == approx(values.front()));

    SECTION("but changes the route a ramped one takes") {
        auto ramped = stepped();
        ramped.shape = 1.0f;

        auto plain = seeded(5);
        walk(plain, ramped, 10);
        const auto straight = walk(plain, ramped, 10);

        ramped.smooth = 1.0f;
        auto smoothed = seeded(5);
        walk(smoothed, ramped, 10);
        const auto eased = walk(smoothed, ramped, 10);

        // The same two ends, because smoothing is about the phase rather than
        // the values the step travels between.
        REQUIRE(straight.size() == eased.size());
        CHECK(eased.front() == approx(straight.front()));

        // And a different route between them.
        bool differs = false;
        for (std::size_t i = 1; i + 1 < straight.size(); ++i)
            differs = differs || std::abs(eased[i] - straight[i]) > 1e-3f;
        CHECK(differs);
    }
}

TEST_CASE("Noise steps every block and ignores the rate", "[engine][mod][random]") {
    auto settings = stepped();
    settings.type = RandomShape::Noise;

    auto state = seeded(13);

    // Ten blocks inside what would be one step at this rate, and ten different
    // values: the rate stops meaning anything, because there is no longer a
    // step for it to be the length of.
    const auto values = walk(state, settings, 10);

    std::set<float> distinct(values.begin(), values.end());
    CHECK(distinct.size() > 1);

    for (const auto value : values) {
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("A walk is the same on every run of a project", "[engine][mod][random]") {
    const auto settings = stepped();

    auto first = seeded(42);
    auto second = seeded(42);
    CHECK(walk(first, settings, 60) == walk(second, settings, 60));

    // And two modifiers are independent, which is what seeding from the
    // modifier's own address buys: adjacent addresses must not walk together.
    auto other = seeded(43);
    auto again = seeded(42);
    CHECK(walk(other, settings, 60) != walk(again, settings, 60));
}

// =============================================================================
// The timing
// =============================================================================

TEST_CASE("A synced walk steps on the bar grid", "[engine][mod][random]") {
    auto settings = stepped();
    settings.sync = ModSync::Transport;
    settings.tempoSync = true;
    settings.rate.rateType = static_cast<int>(ModRateType::Bar);

    auto state = seeded();

    // A timeline-locked walk is a function of where the block is, so two of
    // them at one rate step together however playback got there. Block one bar
    // in and the phase is at the top of a step.
    BlockInfo block = blockOf(64);
    block.startBeat = 4.0;
    block.endBeat = 4.0 + (64.0 / kSampleRate) * 2.0;

    step(state, settings, block);
    CHECK(state.phase == approx(0.0f));

    // Halfway through the next bar is halfway through the step.
    block.startBeat = 6.0;
    step(state, settings, block);
    CHECK(state.phase == approx(0.5f));
}

TEST_CASE("A free-running walk advances by how long the block was", "[engine][mod][random]") {
    auto settings = stepped(2.0f);
    auto state = seeded();

    // Twentieths of a second at two cycles a second is a tenth of a step each,
    // so the tenth block publishes the last position before the wrap and the
    // eleventh publishes the top of the next step. The value a block renders
    // with is the value at its first sample, which is why the wrap shows up in
    // the block after the one that completed it.
    const auto block = blockOf(static_cast<int>(kSampleRate / 20.0));
    for (int i = 0; i < 10; ++i)
        step(state, settings, block);
    CHECK(state.phase == approx(0.9f));

    step(state, settings, block);
    CHECK(state.phase == approx(0.0f));
}

TEST_CASE("A trigger restarts the walk and takes a step", "[engine][mod][random]") {
    auto settings = stepped();
    settings.sync = ModSync::Note;
    settings.trigger = LFOTriggerMode::MIDI;

    auto state = seeded(17);
    walk(state, settings, 5);
    const auto before = state.current;

    restartRandom(state, settings);
    step(state, settings, tenthBlock());

    // Back to the top of a step, and a new number drawn rather than the ramp
    // being replayed towards the one the walk was already heading for.
    CHECK(state.phase == approx(0.0f));
    CHECK(state.current != approx(before));

    SECTION("and a free-running walk ignores it") {
        auto free = stepped();
        auto ignoring = seeded(17);
        walk(ignoring, free, 5);
        const auto phase = ignoring.phase;

        restartRandom(ignoring, free);
        CHECK(!ignoring.stepPending);
        CHECK(ignoring.phase == approx(phase));
    }
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
    // fork drives both through one mapping and a project has to sound the same
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
    // The fork's random modifier resyncs on a note and has no gate parameter,
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
