#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModAdsr.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_mod_adsr.cpp
 * @brief The envelope generator (#2120).
 *
 * Three claims, separable the way the LFO's are. The shape is the fork's: a
 * segment's curvature is a persisted number and what it draws has to be the
 * same curve in both engines. The run is the engine's: which stage the envelope
 * is in after a block, what a gate edge does from each of them, and what a
 * tempo-synced stage is worth in seconds. And the wiring is the table's: that
 * the model's seven fields reach the block and that the value comes out of a
 * device the far end.
 */

using namespace magda;
using magda::engine::adsrSegmentAt;
using magda::engine::AdsrSettings;
using magda::engine::AdsrStage;
using magda::engine::adsrStageSeconds;
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
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;
using magda::engine::restartAdsr;

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

/// An envelope with round stage lengths: ten blocks up, ten down to a half, ten
/// back to nothing, when stepped a millisecond at a time.
AdsrSettings tenTenTen(ModSync sync = ModSync::Note) {
    AdsrSettings settings;
    settings.attackMs = 10.0f;
    settings.decayMs = 10.0f;
    settings.sustain = 0.5f;
    settings.releaseMs = 10.0f;
    settings.sync = sync;
    settings.trigger = sync == ModSync::Note ? LFOTriggerMode::MIDI : LFOTriggerMode::Free;
    return settings;
}

float step(AdsrState& state, const AdsrSettings& settings, const BlockInfo& block,
           const ModTiming& time = timing()) {
    return advanceAdsr(state, settings, block, time);
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

TEST_CASE("A segment with no curvature is a straight line", "[engine][mod][adsr]") {
    CHECK(adsrSegmentAt(0.0f, 0.0f, 1.0f, 0.0f) == approx(0.0f));
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, 0.0f) == approx(0.5f));
    CHECK(adsrSegmentAt(1.0f, 0.0f, 1.0f, 0.0f) == approx(1.0f));

    // Falling as well as rising, and between two values neither of which is an
    // end of the range: a release from a half-open envelope is one of these.
    CHECK(adsrSegmentAt(0.25f, 0.8f, 0.4f, 0.0f) == approx(0.7f));

    // A segment that does not travel has nothing to bend, whatever the
    // curvature says. The bezier would be asked for the y at an x on a line of
    // no height.
    CHECK(adsrSegmentAt(0.5f, 0.3f, 0.3f, 0.4f) == approx(0.3f));
}

TEST_CASE("Curvature bends a segment without moving its ends", "[engine][mod][adsr]") {
    // Whatever the bend, a segment starts where it starts and arrives where it
    // arrives: the curvature is about the route.
    for (const float curve : {-0.5f, -0.25f, 0.25f, 0.5f}) {
        CHECK(adsrSegmentAt(0.0f, 0.0f, 1.0f, curve) == approx(0.0f));
        CHECK(adsrSegmentAt(1.0f, 0.0f, 1.0f, curve) == approx(1.0f));
    }

    // Positive curvature is the exponential side: slow away from the start and
    // fast into the end, so the midpoint sits below the straight line.
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, 0.5f) < 0.5f);

    // Negative is the logarithmic one, and the two are reflections about the
    // line rather than about each other's shape.
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, -0.5f) > 0.5f);

    // Monotonic in between: a bent attack still only rises.
    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const auto value = adsrSegmentAt(static_cast<float>(i) / 20.0f, 0.0f, 1.0f, 0.35f);
        CHECK(value >= previous);
        previous = value;
    }
}

TEST_CASE("A stage runs in milliseconds unless the envelope is synced", "[engine][mod][adsr]") {
    AdsrSettings settings;
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.25));

    // Synced, a stage is the division rather than the time, and the division is
    // the one the model carries for all three stages. A quarter at 120 is half
    // a second.
    settings.tempoSync = true;
    settings.rateType = static_cast<int>(ModRateType::Quarter);
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.5));

    // The signature counts, because a division is a fraction of a bar: a bar of
    // three four is three beats, so a bar-long stage is a second and a half.
    settings.rateType = static_cast<int>(ModRateType::Bar);
    CHECK(adsrStageSeconds(250.0f, settings, timing(120.0, 3, 4)) == Catch::Approx(1.5));

    // Hertz is not a division. The model reaches that by having tempo sync on
    // with nothing musical selected, and the fork's answer is the milliseconds.
    settings.rateType = static_cast<int>(ModRateType::Hertz);
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.25));
}

// =============================================================================
// The run
// =============================================================================

TEST_CASE("A gate opening runs the envelope through its stages", "[engine][mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    // Shut to begin with, because a MIDI-triggered envelope waits for a note.
    state.gated = true;
    state.started = true;
    state.trigger = settings.trigger;

    CHECK(step(state, settings, millisecondBlock()) == approx(0.0f));
    CHECK(state.stage == AdsrStage::Idle);

    state.gated = false;

    // Advanced first and published after, which is the fork's ADSR timer: the
    // value a block renders with is where the envelope ends up. One block into
    // a ten-block attack is a tenth of the way up.
    CHECK(step(state, settings, millisecondBlock()) == approx(0.1f));
    CHECK(state.stage == AdsrStage::Attack);

    // Through the attack and into the decay, which falls from the top to the
    // sustain level over its own ten blocks.
    CHECK(run(state, settings, 9) == approx(1.0f));
    CHECK(state.stage == AdsrStage::Decay);

    CHECK(run(state, settings, 5) == approx(0.75f));
    CHECK(run(state, settings, 5) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Sustain);

    // Held there for as long as the gate stays open, which is what makes it a
    // sustain rather than a stage.
    CHECK(run(state, settings, 50) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Sustain);
}

TEST_CASE("A gate shutting releases from wherever the envelope is", "[engine][mod][adsr]") {
    const auto settings = tenTenTen();

    SECTION("from the sustain") {
        AdsrState state;
        run(state, settings, 25);
        REQUIRE(state.stage == AdsrStage::Sustain);

        state.gated = true;
        CHECK(run(state, settings, 5) == approx(0.25f));
        CHECK(state.stage == AdsrStage::Release);

        CHECK(run(state, settings, 5) == approx(0.0f));
        CHECK(state.stage == AdsrStage::Idle);
    }

    SECTION("from halfway up the attack") {
        AdsrState state;
        const auto reached = run(state, settings, 5);
        REQUIRE(state.stage == AdsrStage::Attack);
        REQUIRE(reached == approx(0.5f));

        // The release starts from where the envelope was, not from the top:
        // that is what makes a gate change click-free from any stage.
        state.gated = true;
        CHECK(run(state, settings, 5) == approx(0.25f));
        CHECK(run(state, settings, 5) == approx(0.0f));
    }
}

TEST_CASE("A free-running envelope cycles without a gate", "[engine][mod][adsr]") {
    const auto settings = tenTenTen(ModSync::Free);
    AdsrState state;

    // No note and no transport, and it runs anyway: the gate is held open, and
    // the sustain is skipped so the cycle is attack, decay, release.
    CHECK(run(state, settings, 10) == approx(1.0f));
    CHECK(run(state, settings, 10) == approx(0.5f));

    // Straight from the sustain into the release rather than resting there.
    CHECK(state.stage == AdsrStage::Release);
    CHECK(run(state, settings, 10) == approx(0.0f));

    // And round again, rather than stopping at idle.
    CHECK(run(state, settings, 5) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Attack);
}

TEST_CASE("A transport-locked envelope is gated by playback", "[engine][mod][adsr]") {
    auto settings = tenTenTen(ModSync::Transport);
    settings.trigger = LFOTriggerMode::Transport;

    AdsrState state;

    // Stopped is shut, and a shut gate that has never opened is silence rather
    // than a release.
    CHECK(run(state, settings, 5, millisecondBlock(false)) == approx(0.0f));
    CHECK(state.stage == AdsrStage::Idle);

    CHECK(run(state, settings, 10, millisecondBlock(true)) == approx(1.0f));

    // Stopping releases, from wherever the envelope had got to.
    CHECK(run(state, settings, 5, millisecondBlock(false)) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Release);
}

TEST_CASE("A stage with no time in it is instant", "[engine][mod][adsr]") {
    auto settings = tenTenTen();
    settings.attackMs = 0.0f;
    AdsrState state;

    // Straight to the top and on into the decay within the same block: an
    // envelope with no attack is a click on purpose, and the rest of the block
    // belongs to the stage after it.
    CHECK(step(state, settings, millisecondBlock()) == approx(0.95f));
    CHECK(state.stage == AdsrStage::Decay);

    SECTION("and an envelope with no time anywhere still terminates") {
        // Every stage instant and free running, so the cycle has no time in it
        // anywhere and the block would walk it for ever. The bound is the
        // fork's own, eight stages, and where in the cycle that leaves the
        // envelope is a consequence of the bound rather than a claim: what
        // matters is that the block ends and the output is a level.
        AdsrSettings instant;
        instant.attackMs = 0.0f;
        instant.decayMs = 0.0f;
        instant.releaseMs = 0.0f;
        instant.sustain = 0.5f;
        instant.sync = ModSync::Free;

        AdsrState spinning;
        const auto value = step(spinning, instant, millisecondBlock());
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("A block longer than a stage lands in the stage after it", "[engine][mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    // A block of twelve milliseconds over a ten-millisecond attack: ten of it
    // is the attack and the other two belong to the decay, which is a fifth of
    // the way from the top to the sustain.
    const auto block = rollingBlock(static_cast<int>(kSampleRate * 0.012));
    CHECK(step(state, settings, block) == approx(0.9f));
    CHECK(state.stage == AdsrStage::Decay);
}

TEST_CASE("A retrigger enters the attack from where the envelope was", "[engine][mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    run(state, settings, 25);
    REQUIRE(state.stage == AdsrStage::Sustain);

    restartAdsr(state, settings, false);
    CHECK(state.stage == AdsrStage::Attack);
    CHECK(state.value == approx(0.5f));

    // Halfway from the sustain to the top after five of the attack's ten
    // blocks, rather than halfway from silence.
    CHECK(run(state, settings, 5) == approx(0.75f));

    SECTION("or from zero, which is what a cross-track trigger asks for") {
        restartAdsr(state, settings, true);
        CHECK(state.value == approx(0.0f));
        CHECK(run(state, settings, 5) == approx(0.5f));
    }
}

TEST_CASE("A mode change retires the gate the old mode left", "[engine][mod][adsr]") {
    auto settings = tenTenTen();
    settings.trigger = LFOTriggerMode::Audio;
    settings.startGated = true;

    AdsrState state;
    CHECK(run(state, settings, 5) == approx(0.0f));
    REQUIRE(state.gated);

    // Switched to free running. Nothing in the new mode ever opens a gate, so
    // an envelope that kept the old one would be silent for ever.
    settings.sync = ModSync::Free;
    settings.trigger = LFOTriggerMode::Free;
    settings.startGated = false;

    CHECK(run(state, settings, 10) == approx(1.0f));
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
