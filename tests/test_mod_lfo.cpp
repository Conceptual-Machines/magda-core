#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "core/ModCurve.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "param/ModLfo.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_mod_lfo.cpp
 * @brief The LFO engine (#2119).
 *
 * Three claims, and they are separable on purpose. The shape is the model's
 * own, so what is asserted about it is that the engine reads the same curve the
 * editor draws rather than what a bezier does. The run is the engine's: where
 * the phase is after a block, what a division is worth in beats, what a trigger
 * does and what a gate does. And the wiring is the table's: that a modifier's
 * settings reach the block, that its rate can be driven, and that the value
 * comes out of a device the far end.
 */

using namespace magda;
using magda::engine::advanceLfo;
using magda::engine::barFractionOf;
using magda::engine::BlockInfo;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::cycleBeats;
using magda::engine::INVALID_PARAM_ID;
using magda::engine::LfoRate;
using magda::engine::LfoSettings;
using magda::engine::LfoState;
using magda::engine::ModContribution;
using magda::engine::ModKind;
using magda::engine::ModRuntime;
using magda::engine::ModSync;
using magda::engine::ModTiming;
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamStep;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;
using magda::engine::restartLfo;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

ModTiming timing(double bpm = 120.0, int numerator = 4, int denominator = 4) {
    return ModTiming{kSampleRate, bpm, numerator, denominator};
}

/// A block of @p numSamples that does not move the timeline, which is what a
/// free-running LFO is measured against: its ramp is real time rather than
/// musical time, and a stopped transport still turns it.
BlockInfo stoppedBlock(int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    return block;
}

/// A block sitting at @p startBeat, with the seconds face of the same instant
/// worked out at @p bpm, the way the transport's clock would.
BlockInfo blockAt(double startBeat, double beats, int numSamples, double bpm = 120.0) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startBeat = startBeat;
    block.endBeat = startBeat + beats;
    block.startSeconds = startBeat * 60.0 / bpm;
    block.endSeconds = block.endBeat * 60.0 / bpm;
    return block;
}

LfoSettings freeRunning(float hz, LFOWaveform wave = LFOWaveform::Saw) {
    LfoSettings settings;
    settings.wave = wave;
    settings.sync = ModSync::Free;
    settings.rate.hz = hz;
    return settings;
}

/// One block of @p settings, and the output it published.
float step(LfoState& state, const LfoSettings& settings, const BlockInfo& block,
           std::span<const CurvePointData> curve = {}, const ModTiming& time = timing()) {
    return advanceLfo(state, settings, curve, block, time);
}

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.macros = createDefaultMacros(2);
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeDevice(DeviceId id, float base = 0.0f) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.macros = createDefaultMacros(1);
    device.mods = createDefaultMods(0);

    ParameterInfo info(0, "Level", "", 0.0f, 1.0f, 0.0f);
    info.currentValue = base;
    device.parameters.push_back(info);

    return device;
}

/// A track with one device and one LFO wired to that device's only parameter.
TrackInfo trackWithLfo(float amount = 1.0f, bool bipolar = false, float base = 0.0f) {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, base)));
    track.mods = createDefaultMods(1);
    track.mods[0].waveform = LFOWaveform::Saw;
    track.mods[0].rate = 1.0f;
    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), amount, bipolar, true});
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

ParamKey modRate(TrackId track, ModId mod) {
    ParamKey key;
    key.kind = ParamKey::Kind::ModParam;
    key.scope = ParamKey::Scope::Track;
    key.trackId = track;
    key.modId = mod;
    key.index = 0;
    return key;
}

ParamTable tableFor(const std::vector<TrackInfo>& tracks,
                    const std::vector<AutomationLaneInfo>& lanes = {}) {
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master, lanes);
}

/// Everything one block needs to resolve a table, kept together so a test can
/// run several blocks over one runtime and watch the phase move.
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

bool mentions(const ParamTable& table, const std::string& text) {
    for (const auto& message : table.diagnostics)
        if (message.find(text) != std::string::npos)
            return true;
    return false;
}

}  // namespace

// =============================================================================
// The shape
// =============================================================================

TEST_CASE("Every waveform is read at the same points in the cycle", "[engine][mod][lfo]") {
    const auto at = [](LFOWaveform wave, float phase) {
        LfoState state;
        auto settings = freeRunning(1.0f, wave);
        settings.phaseOffset = phase;
        // A block with no samples in it cannot advance anything, so what comes
        // out is the shape at the phase and nothing else.
        return step(state, settings, stoppedBlock(0));
    };

    SECTION("sine rises from the middle") {
        CHECK(at(LFOWaveform::Sine, 0.0f) == approx(0.5f));
        CHECK(at(LFOWaveform::Sine, 0.25f) == approx(1.0f));
        CHECK(at(LFOWaveform::Sine, 0.5f) == approx(0.5f));
        CHECK(at(LFOWaveform::Sine, 0.75f) == approx(0.0f));
    }

    SECTION("triangle peaks halfway") {
        CHECK(at(LFOWaveform::Triangle, 0.0f) == approx(0.0f));
        CHECK(at(LFOWaveform::Triangle, 0.5f) == approx(1.0f));
        CHECK(at(LFOWaveform::Triangle, 0.75f) == approx(0.5f));
    }

    SECTION("square is high for the first half") {
        CHECK(at(LFOWaveform::Square, 0.0f) == approx(1.0f));
        CHECK(at(LFOWaveform::Square, 0.49f) == approx(1.0f));
        CHECK(at(LFOWaveform::Square, 0.5f) == approx(0.0f));
    }

    SECTION("saw climbs and reverse saw falls") {
        CHECK(at(LFOWaveform::Saw, 0.25f) == approx(0.25f));
        CHECK(at(LFOWaveform::ReverseSaw, 0.25f) == approx(0.75f));
    }
}

TEST_CASE("A drawn cycle is read through the model's own curve", "[engine][mod][lfo]") {
    LfoSettings settings = freeRunning(1.0f, LFOWaveform::Custom);

    // A step and a ramp, which is a shape the built-in waveforms cannot make
    // and which the fork reads through the same function.
    std::vector<CurvePointData> curve(3);
    curve[0] = CurvePointData{0.0f, 0.0f};
    curve[0].curveType = 2;  // Step: holds until the next point
    curve[1] = CurvePointData{0.5f, 1.0f};
    curve[2] = CurvePointData{0.75f, 0.0f};

    const auto at = [&](float phase) {
        LfoState state;
        auto shaped = settings;
        shaped.phaseOffset = phase;
        return step(state, shaped, stoppedBlock(0), curve);
    };

    CHECK(at(0.25f) == approx(0.0f));
    CHECK(at(0.5f) == approx(1.0f));
    CHECK(at(0.625f) == approx(magda::modcurve::points(curve, 0.625f)));
}

TEST_CASE("A custom waveform with nothing drawn on it falls back to its preset",
          "[engine][mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Custom);
    settings.preset = CurvePreset::RampDown;
    settings.phaseOffset = 0.25f;

    CHECK(step(state, settings, stoppedBlock(0)) == approx(0.75f));
}

// =============================================================================
// The run
// =============================================================================

TEST_CASE("A free-running LFO advances by how long the block lasted", "[engine][mod][lfo]") {
    LfoState state;
    const auto settings = freeRunning(2.0f);  // two cycles a second

    // A quarter of a second at 48k, which is half a cycle at 2 Hz.
    const auto block = stoppedBlock(12000);

    CHECK(step(state, settings, block) == approx(0.0f));
    CHECK(step(state, settings, block) == approx(0.5f));
    CHECK(step(state, settings, block) == approx(0.0f));
}

TEST_CASE("A free-running LFO keeps turning while the transport is stopped", "[engine][mod][lfo]") {
    LfoState state;
    const auto settings = freeRunning(1.0f);

    // The block does not move the timeline at all, which is what a stopped
    // transport renders. The graph is still processing, so the LFO still moves.
    const auto block = stoppedBlock(12000);
    step(state, settings, block);

    CHECK(step(state, settings, block) == approx(0.25f));
}

TEST_CASE("A transport-locked LFO is a function of where the block is", "[engine][mod][lfo]") {
    auto settings = freeRunning(1.0f);
    settings.sync = ModSync::Transport;

    // Half a second in at 1 Hz is halfway through the cycle, whatever was
    // rendered before it: two LFOs at one rate agree however playback got here.
    LfoState fresh;
    CHECK(step(fresh, settings, blockAt(1.0, 0.5, 512)) == approx(0.5f));

    LfoState played;
    step(played, settings, blockAt(0.0, 0.5, 512));
    CHECK(step(played, settings, blockAt(1.0, 0.5, 512)) == approx(0.5f));
}

TEST_CASE("A tempo-synced LFO's period is a fraction of a bar", "[engine][mod][lfo]") {
    SECTION("the ordinals are the ones a project stores") {
        CHECK(barFractionOf(static_cast<int>(ModRateType::Bar)) == Catch::Approx(1.0));
        CHECK(barFractionOf(static_cast<int>(ModRateType::FourBars)) == Catch::Approx(4.0));
        CHECK(barFractionOf(static_cast<int>(ModRateType::Half)) == Catch::Approx(0.5));
        CHECK(barFractionOf(static_cast<int>(ModRateType::DottedQuarter)) ==
              Catch::Approx(0.25 * 1.5));
        CHECK(barFractionOf(static_cast<int>(ModRateType::TripletEighth)) ==
              Catch::Approx(0.125 * 2.0 / 3.0));

        // Hertz is not a division, and neither is an ordinal from the future.
        CHECK(barFractionOf(static_cast<int>(ModRateType::Hertz)) == Catch::Approx(1.0));
        CHECK(barFractionOf(9999) == Catch::Approx(1.0));
    }

    SECTION("a bar is the signature's own bar") {
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 4, 4) == Catch::Approx(4.0));
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 3, 4) == Catch::Approx(3.0));
        CHECK(cycleBeats(static_cast<int>(ModRateType::Half), 4, 4) == Catch::Approx(2.0));

        // The denominator counts. A bar of six eight is three quarter notes,
        // and a beat is a quarter note, so it is three beats rather than six.
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 6, 8) == Catch::Approx(3.0));
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 3, 8) == Catch::Approx(1.5));
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 12, 8) == Catch::Approx(6.0));
        CHECK(cycleBeats(static_cast<int>(ModRateType::Bar), 2, 2) == Catch::Approx(4.0));
    }

    SECTION("locked to the bar line") {
        LfoState state;
        LfoSettings settings;
        settings.wave = LFOWaveform::Saw;
        settings.sync = ModSync::Transport;
        settings.tempoSync = true;
        settings.rate.rateType = static_cast<int>(ModRateType::Bar);

        CHECK(step(state, settings, blockAt(0.0, 1.0, 512)) == approx(0.0f));
        CHECK(step(state, settings, blockAt(2.0, 1.0, 512)) == approx(0.5f));
        CHECK(step(state, settings, blockAt(4.0, 1.0, 512)) == approx(0.0f));
        CHECK(step(state, settings, blockAt(5.0, 1.0, 512)) == approx(0.25f));
    }

    SECTION("in three four a bar is three beats") {
        LfoState state;
        LfoSettings settings;
        settings.wave = LFOWaveform::Saw;
        settings.sync = ModSync::Transport;
        settings.tempoSync = true;
        settings.rate.rateType = static_cast<int>(ModRateType::Bar);

        CHECK(step(state, settings, blockAt(1.5, 1.0, 512), {}, timing(120.0, 3, 4)) ==
              approx(0.5f));
    }

    SECTION("in six eight a bar is three beats, not six") {
        // The fork used to count the numerator in quarter notes, so a "1 Bar"
        // modifier here ran two written bars long. Both engines read the
        // denominator now (#2128).
        LfoState state;
        LfoSettings settings;
        settings.wave = LFOWaveform::Saw;
        settings.sync = ModSync::Transport;
        settings.tempoSync = true;
        settings.rate.rateType = static_cast<int>(ModRateType::Bar);

        // A bar and a half in, which is halfway through the second cycle.
        CHECK(step(state, settings, blockAt(4.5, 0.5, 512), {}, timing(120.0, 6, 8)) ==
              approx(0.5f));

        // And the bar line itself opens a cycle.
        LfoState onTheBar;
        CHECK(step(onTheBar, settings, blockAt(3.0, 0.5, 512), {}, timing(120.0, 6, 8)) ==
              approx(0.0f));
    }

    SECTION("free-running, the period is that many beats at this tempo") {
        LfoState state;
        LfoSettings settings;
        settings.wave = LFOWaveform::Saw;
        settings.sync = ModSync::Free;
        settings.tempoSync = true;
        settings.rate.rateType = static_cast<int>(ModRateType::Bar);

        // A bar at 120 in four four is two seconds, so half a second is a
        // quarter of the way through.
        const auto block = stoppedBlock(24000);
        step(state, settings, block);
        CHECK(step(state, settings, block) == approx(0.25f));
    }
}

TEST_CASE("The phase offset moves where the cycle is read", "[engine][mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f);
    settings.phaseOffset = 0.25f;

    CHECK(step(state, settings, stoppedBlock(12000)) == approx(0.25f));
    CHECK(step(state, settings, stoppedBlock(12000)) == approx(0.5f));
}

TEST_CASE("A one-shot plays through and holds where it ended", "[engine][mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Triangle);
    settings.sync = ModSync::Note;
    settings.oneShot = true;

    const auto quarter = stoppedBlock(12000);

    CHECK(step(state, settings, quarter) == approx(0.0f));
    CHECK(step(state, settings, quarter) == approx(0.5f));
    CHECK(step(state, settings, quarter) == approx(1.0f));
    CHECK(step(state, settings, quarter) == approx(0.5f));

    // Through. A triangle ends where it started, and that is what is held
    // rather than the wrap-around value the cycle would carry on into.
    CHECK(step(state, settings, quarter) == approx(0.0f));
    CHECK(state.completed);
    CHECK(state.phase == approx(1.0f));
    CHECK(step(state, settings, quarter) == approx(0.0f));

    SECTION("and a trigger plays it again") {
        restartLfo(state, settings);
        CHECK_FALSE(state.completed);
        CHECK(step(state, settings, quarter) == approx(0.0f));
        CHECK(step(state, settings, quarter) == approx(0.5f));
    }
}

TEST_CASE("A sustain loop plays the intro once and then repeats the region", "[engine][mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Custom);
    settings.oneShot = true;
    settings.useLoopRegion = true;
    settings.loopStart = 0.5f;
    settings.loopEnd = 0.75f;

    // A straight ramp, so the value reads back as the position in the cycle.
    std::vector<CurvePointData> curve(2);
    curve[0] = CurvePointData{0.0f, 0.0f};
    curve[1] = CurvePointData{1.0f, 1.0f};

    const auto eighth = stoppedBlock(6000);

    CHECK(step(state, settings, eighth, curve) == approx(0.0f));
    CHECK(step(state, settings, eighth, curve) == approx(0.125f));
    CHECK(step(state, settings, eighth, curve) == approx(0.25f));
    CHECK(step(state, settings, eighth, curve) == approx(0.375f));

    // Into the region, and back to its start rather than on past its end.
    CHECK(step(state, settings, eighth, curve) == approx(0.5f));
    CHECK(step(state, settings, eighth, curve) == approx(0.625f));
    CHECK(step(state, settings, eighth, curve) == approx(0.5f));
    CHECK(step(state, settings, eighth, curve) == approx(0.625f));

    // A loop sustains rather than finishing, so the one-shot never latches.
    CHECK_FALSE(state.completed);
}

TEST_CASE("A level curve is applied as the amount it takes away", "[engine][mod][lfo]") {
    auto settings = freeRunning(1.0f, LFOWaveform::Saw);
    settings.invertOutput = true;
    settings.phaseOffset = 0.25f;

    LfoState state;
    CHECK(step(state, settings, stoppedBlock(0)) == approx(0.75f));

    SECTION("and a shut gate is still nothing rather than everything") {
        // The convention the whole modulation system reads: an inactive
        // modifier outputs 0, which for a level curve means no attenuation.
        state.gated = true;
        CHECK(step(state, settings, stoppedBlock(0)) == approx(0.0f));
    }
}

// =============================================================================
// Triggers and gates
// =============================================================================

namespace {

/// A table with one note-triggered LFO on track 1, and its index.
struct Triggered {
    ParamTable table;
    int modifier = 0;
};

Triggered triggered(LFOTriggerMode mode, bool skipNativeResync = false) {
    auto track = trackWithLfo();
    track.mods[0].triggerMode = mode;

    Triggered value;
    value.table = tableFor({track});
    value.table.modifiers[0].lfo.skipNativeResync = skipNativeResync;

    // By address rather than by position, which is how anything holding a
    // modifier rather than an index finds it.
    ParamKey key = modRate(1, 0);
    key.index = -1;

    ModRuntime runtime;
    runtime.prepare(value.table, magda::engine::RenderContext{kSampleRate, 512, 2});
    value.modifier = runtime.indexOf(key);
    return value;
}

}  // namespace

TEST_CASE("A note-triggered LFO restarts on a note and rests between them",
          "[engine][mod][lfo][trigger]") {
    auto fixture = triggered(LFOTriggerMode::MIDI);
    REQUIRE(fixture.modifier >= 0);

    Harness harness(fixture.table);

    const auto param = fixture.table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);

    const auto block = stoppedBlock(12000);  // a quarter second, a quarter cycle at 1 Hz

    // Nothing has played, so the gate is shut and the modifier is doing
    // nothing at all.
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
    CHECK(harness.values[param].value() == approx(0.0f));

    harness.mods.noteOn(fixture.modifier, fixture.table);
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.25f));

    SECTION("a second note restarts the phase") {
        harness.mods.noteOn(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
    }

    SECTION("the gate shuts when the last held note lifts") {
        // A second note held on top of the first, so lifting one is not
        // lifting them all. Two blocks on, the saw is a quarter of the way up,
        // which is a value a shut gate could not produce.
        harness.mods.noteOn(fixture.modifier, fixture.table);
        harness.mods.noteOff(fixture.modifier, fixture.table);

        harness.run(fixture.table, block);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.25f));

        harness.mods.noteOff(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
        CHECK(harness.values[param].value() == approx(0.0f));
    }

    SECTION("an all-notes-off drops every held note at once") {
        harness.mods.noteOn(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        harness.run(fixture.table, block);
        REQUIRE(harness.mods.value(fixture.modifier) == approx(0.25f));

        harness.mods.allNotesOff(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
    }
}

TEST_CASE("A cross-track LFO ignores the track it modulates", "[engine][mod][lfo][trigger]") {
    auto fixture = triggered(LFOTriggerMode::MIDI, /*skipNativeResync=*/true);
    fixture.table.modifiers[0].lfo.gateOnTrigger = false;
    fixture.table.modifiers[0].lfo.startGated = false;

    Harness harness(fixture.table);
    const auto block = stoppedBlock(12000);

    harness.run(fixture.table, block);
    harness.run(fixture.table, block);
    REQUIRE(harness.mods.value(fixture.modifier) == approx(0.25f));

    SECTION("a note on its own track does nothing") {
        harness.mods.noteOn(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.5f));
    }

    SECTION("nor does one that would otherwise gate it") {
        // The fork prevents this pairing by setting its two flags apart rather
        // than by refusing it. Refusing it here is what keeps the destination
        // track's notes off a cross-track LFO's gate whatever sets the flags.
        auto gating = fixture.table;
        gating.modifiers[0].lfo.gateOnTrigger = true;

        harness.mods.noteOn(fixture.modifier, gating);
        harness.run(gating, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.5f));

        harness.mods.noteOff(fixture.modifier, gating);
        harness.run(gating, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.75f));
    }

    SECTION("a trigger from its source restarts it") {
        harness.mods.trigger(fixture.modifier, fixture.table);
        harness.run(fixture.table, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));
    }
}

TEST_CASE("A forced zero stands in for the gap a gated retrigger leaves",
          "[engine][mod][lfo][trigger]") {
    auto fixture = triggered(LFOTriggerMode::Audio);
    fixture.table.modifiers[0].lfo.startGated = false;

    Harness harness(fixture.table);
    const auto block = stoppedBlock(12000);
    harness.run(fixture.table, block);
    harness.run(fixture.table, block);
    REQUIRE(harness.mods.value(fixture.modifier) == approx(0.25f));

    // The trigger lands inside a block whose parameters are already resolved,
    // so the gap is published on the next one and the shape starts over on the
    // one after: the fork's own sequence, late by the block the trigger is.
    harness.mods.trigger(fixture.modifier, fixture.table, /*forceZero=*/true);

    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.25f));

    SECTION("and the gap is a gap in a shape that does not open at nothing") {
        // A saw opens at zero either way, so it cannot tell a gap from a
        // restart. A sine opens halfway up, and the gap is the whole point.
        auto sine = fixture.table;
        sine.modifiers[0].lfo.wave = LFOWaveform::Sine;

        harness.run(sine, block);
        REQUIRE(harness.mods.value(fixture.modifier) != approx(0.0f));

        harness.mods.trigger(fixture.modifier, sine, /*forceZero=*/true);
        harness.run(sine, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

        harness.run(sine, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.5f));
    }

    SECTION("but never on a level curve, where zero is full level") {
        auto level = fixture.table;
        level.modifiers[0].lfo.invertOutput = true;

        harness.run(level, block);
        const auto before = harness.mods.value(fixture.modifier);

        harness.mods.trigger(fixture.modifier, level, /*forceZero=*/true);
        harness.run(level, block);

        // Restarted, and never zero on the way: forcing full level in the
        // middle of a duck is a click on every hit.
        CHECK(harness.mods.value(fixture.modifier) == approx(1.0f));
        CHECK(before != approx(0.0f));
    }
}

TEST_CASE("Changing the trigger mode retires the gate the old one left",
          "[engine][mod][lfo][trigger]") {
    // An audio-triggered LFO sits shut between hits, and a trigger mode is a
    // values publish rather than a structural one, so the runtime is never
    // re-prepared for it. Without reconciling, switching to free running would
    // leave the gate shut for ever: nothing in the new mode ever opens one.
    auto fixture = triggered(LFOTriggerMode::Audio);
    Harness harness(fixture.table);
    const auto block = stoppedBlock(12000);

    harness.run(fixture.table, block);
    harness.run(fixture.table, block);
    REQUIRE(harness.mods.value(fixture.modifier) == approx(0.0f));

    auto freed = fixture.table;
    freed.modifiers[0].lfo.trigger = LFOTriggerMode::Free;
    freed.modifiers[0].lfo.sync = ModSync::Free;
    freed.modifiers[0].lfo.startGated = false;
    freed.modifiers[0].lfo.gateOnTrigger = false;

    harness.run(freed, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.5f));

    SECTION("and the held notes with it") {
        auto midi = freed;
        midi.modifiers[0].lfo.trigger = LFOTriggerMode::MIDI;
        midi.modifiers[0].lfo.sync = ModSync::Note;
        midi.modifiers[0].lfo.gateOnTrigger = true;
        midi.modifiers[0].lfo.startGated = true;

        // Shut again on the mode change, and one note-off cannot open it by
        // taking a count left over from the mode before.
        harness.run(midi, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

        harness.mods.noteOff(fixture.modifier, midi);
        harness.run(midi, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

        harness.mods.noteOn(fixture.modifier, midi);
        harness.run(midi, block);
        harness.run(midi, block);
        CHECK(harness.mods.value(fixture.modifier) == approx(0.25f));
    }
}

TEST_CASE("An LFO that is not listening for a trigger ignores one", "[engine][mod][lfo][trigger]") {
    for (const auto sync : {ModSync::Free, ModSync::Transport}) {
        LfoState state;
        auto settings = freeRunning(1.0f);
        settings.sync = sync;

        state.cycles = 0.5;
        restartLfo(state, settings);
        CHECK(state.cycles == Catch::Approx(0.5));
    }
}

TEST_CASE("A gate holds the output at nothing while the phase carries on",
          "[engine][mod][lfo][trigger]") {
    auto fixture = triggered(LFOTriggerMode::Audio);
    Harness harness(fixture.table);
    const auto block = stoppedBlock(12000);

    // Audio-triggered LFOs sit shut between hits.
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

    harness.run(fixture.table, block);
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.0f));

    // Ungating resumes where the LFO would have been rather than where it was
    // when the gate shut: gating is not a pause.
    harness.mods.setGated(fixture.modifier, false);
    harness.run(fixture.table, block);
    CHECK(harness.mods.value(fixture.modifier) == approx(0.75f));
}

// =============================================================================
// The table
// =============================================================================

TEST_CASE("A modifier's settings reach the table", "[engine][mod][lfo][table]") {
    auto track = trackWithLfo();
    track.mods[0].waveform = LFOWaveform::Custom;
    track.mods[0].curvePoints = {{0.0f, 0.0f}, {0.5f, 1.0f}};
    track.mods[0].tempoSync = true;
    track.mods[0].syncDivision = SyncDivision::Eighth;
    track.mods[0].phaseOffset = 0.125f;
    track.mods[0].oneShot = true;
    track.mods[0].invertOutput = true;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    const auto& modifier = table.modifiers[0];
    CHECK(modifier.kind == ModKind::Lfo);
    CHECK(modifier.lfo.wave == LFOWaveform::Custom);
    CHECK(modifier.lfo.tempoSync);
    CHECK(modifier.lfo.rate.rateType == static_cast<int>(ModRateType::Eighth));
    CHECK(modifier.lfo.phaseOffset == approx(0.125f));
    CHECK(modifier.lfo.oneShot);
    CHECK(modifier.lfo.invertOutput);

    // Tempo sync on its own locks the LFO to the timeline, which is the fork's
    // own folding of trigger mode and sync flag into one sync type.
    CHECK(modifier.lfo.sync == ModSync::Transport);
    CHECK(table.modCurveFor(0).size() == 2);
}

TEST_CASE("A modifier the model has switched off has no links at all",
          "[engine][mod][lfo][table]") {
    // Halfway up, so a contribution that pushed the parameter down would show
    // rather than being clamped away at the bottom of the range.
    auto track = trackWithLfo(0.5f, /*bipolar=*/true, /*base=*/0.5f);
    track.mods[0].enabled = false;

    const auto table = tableFor({track});
    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);

    // A bipolar link reading an output of zero would push its parameter down
    // by the link's own depth, which is a switched-off modifier doing
    // something. The fork creates no modifier for one, so nothing contributes.
    CHECK(table.linksFor(param).empty());

    Harness harness(table);
    harness.run(table, stoppedBlock(64));
    CHECK(harness.values[param].value() == approx(0.5f));
}

TEST_CASE("A modifier's rate is carried only when something reaches it",
          "[engine][mod][lfo][table]") {
    SECTION("nothing does") {
        const auto table = tableFor({trackWithLfo()});
        REQUIRE(table.modifiers.size() == 1);
        CHECK(table.modifiers[0].rate == INVALID_PARAM_ID);
        CHECK(table.find(modRate(1, 0)) == INVALID_PARAM_ID);
    }

    SECTION("a macro drives it") {
        auto track = trackWithLfo();
        track.macros[0].value = 0.0f;
        track.macros[0].links.push_back(
            MacroLink{ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0), 1.0f, false});

        const auto table = tableFor({track});
        REQUIRE(table.diagnostics.empty());

        const auto rate = table.find(modRate(1, 0));
        REQUIRE(rate != INVALID_PARAM_ID);
        CHECK(table.modifiers[0].rate == rate);

        // And the modifier resolves after it: that is what the order is for.
        const auto position = [&](const ParamStep& step) {
            return std::find(table.order.begin(), table.order.end(), step) - table.order.begin();
        };
        CHECK(position(ParamStep{ParamStep::Kind::Parameter, rate}) <
              position(ParamStep{ParamStep::Kind::Modifier, 0}));
        CHECK(position(ParamStep{ParamStep::Kind::Modifier, 0}) <
              position(ParamStep{ParamStep::Kind::Parameter, table.find(deviceParam(1, 7, 0))}));
    }

    SECTION("a lane plays over it") {
        auto track = trackWithLfo();

        AutomationLaneInfo lane;
        lane.id = 1;
        lane.target = ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0);
        lane.type = AutomationLaneType::Absolute;
        lane.authorityState = AutomationAuthorityState::Reading;
        lane.absolutePoints = {{1, 0.0, 0.5}, {2, 4.0, 1.0}};

        const auto table = tableFor({track}, {lane});
        CHECK(table.find(modRate(1, 0)) != INVALID_PARAM_ID);
    }
}

TEST_CASE("A driven rate changes how fast the LFO turns", "[engine][mod][lfo][table]") {
    auto track = trackWithLfo();
    track.mods[0].rate = 1.0f;
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0), 1.0f, false});

    const auto table = tableFor({track});
    const auto rate = table.find(modRate(1, 0));
    REQUIRE(rate != INVALID_PARAM_ID);

    Harness harness(table);
    const auto block = stoppedBlock(4800);  // a tenth of a second

    harness.run(table, block);
    // The macro is at the top of its range, so the rate is the top of the
    // lane's: 20 Hz, which is two whole cycles in a tenth of a second.
    CHECK(harness.values[rate].value() == approx(20.0f));

    harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(0.0f));
}

TEST_CASE("A cycle through a modifier's rate is broken where it is found",
          "[engine][mod][lfo][table]") {
    auto track = trackWithLfo();

    // The LFO drives a macro, and that macro drives the LFO's own rate.
    track.mods[0].links.push_back(
        ModLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 0), 0.5f, false, true});
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0), 1.0f, false});

    const auto table = tableFor({track});
    CHECK(mentions(table, "modulation cycle"));

    // The rate parameter survives; only the edge that made it impossible goes.
    CHECK(table.find(modRate(1, 0)) != INVALID_PARAM_ID);
    CHECK(table.modifiers[0].rate == INVALID_PARAM_ID);
    CHECK(table.order.size() == static_cast<std::size_t>(table.size()) + table.modifiers.size());
}

TEST_CASE("The modifier fingerprint moves when the list does", "[engine][mod][lfo][table]") {
    const auto one = tableFor({trackWithLfo()});

    auto grown = trackWithLfo();
    grown.mods = createDefaultMods(2);
    grown.mods[0] = trackWithLfo().mods[0];
    const auto two = tableFor({grown});

    CHECK(one.modifierFingerprint != two.modifierFingerprint);

    // And it does not move when only a value does, which is what stops a knob
    // turn from restarting every LFO in the project.
    auto turned = trackWithLfo();
    turned.mods[0].rate = 4.0f;
    CHECK(tableFor({turned}).modifierFingerprint == one.modifierFingerprint);

    SECTION("and a modifier that becomes another kind is a different list") {
        // The model switches a type in place, at the same address, so nothing
        // else about the table moves. Without the kind in here it would travel
        // as a values publish that the runtime is never re-prepared for, and
        // switching away and back would resume the phase of the modifier that
        // had been replaced.
        auto retyped = trackWithLfo();
        retyped.mods[0].type = ModType::Random;
        CHECK(tableFor({retyped}).modifierFingerprint != one.modifierFingerprint);
    }
}

// =============================================================================
// The runtime
// =============================================================================

TEST_CASE("An LFO carries its phase across a prepare", "[engine][mod][lfo][runtime]") {
    const auto table = tableFor({trackWithLfo()});
    const magda::engine::RenderContext context{kSampleRate, 512, 2};

    ModRuntime first;
    first.prepare(table, context);

    std::vector<ModContribution> links(1);
    std::vector<ParamSegment> segments(magda::engine::ResolvedParams::kDefaultSegmentCapacity);
    ResolvedParams values;
    values.prepare(table.size());

    const auto block = stoppedBlock(12000);
    resolveParams(table, values, links, segments, block, &first);
    resolveParams(table, values, links, segments, block, &first);
    REQUIRE(first.value(0) == approx(0.25f));

    ModRuntime second;
    second.prepare(table, context, &first);
    resolveParams(table, values, links, segments, block, &second);
    CHECK(second.value(0) == approx(0.5f));

    SECTION("but not when the modifier has become something else") {
        auto changed = table;
        changed.modifiers[0].kind = ModKind::Adsr;

        ModRuntime third;
        third.prepare(changed, context, &first);
        REQUIRE(third.state(0) != nullptr);
        CHECK(third.state(0)->lfo.cycles == Catch::Approx(0.0));
    }
}

TEST_CASE("An executor takes over the modifiers of the one it replaces",
          "[engine][mod][lfo][runtime]") {
    auto tracks = std::vector{trackWithLfo()};
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(plan, tracks, master, values);
    REQUIRE(values.params != nullptr);

    const magda::engine::RenderContext context{kSampleRate, 512, 2};
    magda::engine::PlanBindings bindings;

    // The device op has no binding here, which prepare reports and carries on
    // from: what is being pinned is the carry, not the render.
    magda::engine::PlanExecutor first;
    first.prepare(plan, bindings, context, nullptr, values.params.get());
    CHECK(first.carriedModifiers() == 0);

    magda::engine::PlanExecutor second;
    second.prepare(plan, bindings, context, &first, values.params.get());
    CHECK(second.carriedModifiers() == 1);

    SECTION("but not one that has become something else") {
        auto changed = *values.params;
        changed.modifiers[0].kind = ModKind::Random;

        magda::engine::PlanExecutor third;
        third.prepare(plan, bindings, context, &first, &changed);
        CHECK(third.carriedModifiers() == 0);
    }
}

TEST_CASE("A runtime sized for another list leaves the modifiers where they were",
          "[engine][mod][lfo][runtime]") {
    const auto table = tableFor({trackWithLfo()});

    // A runtime that holds nothing, meeting a table with a modifier in it.
    ModRuntime mismatched;

    ResolvedParams values;
    values.prepare(table.size());
    std::vector<ModContribution> links(1);
    std::vector<ParamSegment> segments(magda::engine::ResolvedParams::kDefaultSegmentCapacity);

    resolveParams(table, values, links, segments, stoppedBlock(512), &mismatched);

    // The model's own reading, held, which is what a modifier published before
    // it had an engine at all.
    const auto param = table.find(deviceParam(1, 7, 0));
    CHECK(values[param].value() == approx(table.modifiers[0].value));
}

// =============================================================================
// End to end
// =============================================================================

namespace {

/// A device whose output is its only parameter, so what leaves the master is
/// what the modulation did to it.
class LevelDevice final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock& block) override {
        block.audio.fill(block.params.size() > 0 ? block.params[0].value() : 0.0f);
    }
};

class LevelFactory final : public magda::engine::RuntimeStateFactory {
  public:
    std::unique_ptr<magda::engine::EngineDevice> createDevice(magda::engine::DeviceKey) override {
        return std::make_unique<LevelDevice>();
    }
};

}  // namespace

TEST_CASE("An LFO moves a device parameter through a live session", "[engine][mod][lfo][session]") {
    auto track = trackWithLfo();
    track.mods[0].rate = 2.0f;  // two cycles a second
    track.volume = 1.0f;
    track.pan = 0.0f;

    std::vector<TrackInfo> tracks{track};
    auto master = makeMaster();
    master.volume = 1.0f;

    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values);
    REQUIRE(values.params != nullptr);
    REQUIRE(values.params->modifiers.size() == 1);

    LevelFactory factory;
    magda::engine::EngineSession session(factory);

    // Blocks of a quarter of a second, which at 2 Hz is half a cycle each.
    constexpr int kBlock = 12000;
    const auto ids = magda::engine::collectRuntimeStateIds(tracks, master);
    const magda::engine::RenderContext context{kSampleRate, kBlock, 2};
    REQUIRE(session.publish(plan, context, ids, values).published);

    const auto gain = magda::engine::faderGainFromVolume(track.volume) *
                      magda::engine::faderGainFromVolume(master.volume);

    const auto render = [&] {
        juce::AudioBuffer<float> output(2, kBlock);
        session.process(kBlock, output);
        return output.getSample(0, 0);
    };

    // A saw from nothing, half a cycle at a time: 0, then halfway up.
    CHECK(render() == approx(0.0f));
    CHECK(render() == approx(0.5f * gain));
    CHECK(render() == approx(0.0f));

    SECTION("and goes on turning through a structural republish") {
        REQUIRE(render() == approx(0.5f * gain));

        magda::engine::PlanValues republished;
        magda::engine::resolvePlanValues(*plan, tracks, master, republished);
        const auto result = session.publish(plan, context, ids, std::move(republished));
        REQUIRE(result.published);

        // Where it was, plus the block: an LFO carried across a swap is the
        // same LFO rather than a copy of one read at some instant.
        CHECK(render() == approx(0.0f));
        CHECK(render() == approx(0.5f * gain));
    }
}
