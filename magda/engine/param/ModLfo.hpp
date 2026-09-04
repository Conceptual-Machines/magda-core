#pragma once

#include <cstdint>
#include <span>

#include "core/ModInfo.hpp"
#include "exec/RenderContext.hpp"

/**
 * @file ModLfo.hpp
 * @brief LFO modifier runtime: advances LfoState by one block per LfoSettings.
 *
 * See docs/architecture/modifier-lfo.md for the settings/state split,
 * block-rate timing, bar-relative sync math, and the skipNativeResync /
 * forceZero invariants.
 */

namespace magda::engine {

/**
 * @brief What drives a modifier's output.
 *
 * Only Lfo runs so far; the rest read their model value and hold it,
 * unchanged from before this slice (#2120).
 */
enum class ModKind : std::uint8_t { Lfo, Adsr, Random, Follower };

/**
 * @brief Where a modifier's phase comes from.
 *
 * Folds the model's separate trigger mode and tempo-sync flags into one
 * choice (ModifierHelpers::mapSyncType).
 */
enum class ModSync : std::uint8_t {
    /// A free-running ramp, advanced by block length. Keeps moving while
    /// the transport is stopped.
    Free,

    /// Locked to the timeline, so two LFOs at one rate stay in phase.
    Transport,

    /// A free-running ramp restarted by a trigger (note-on or sidechain).
    Note,
};

/** @brief The rate of one modifier, as the block reads it. */
struct LfoRate {
    /// Cycles per second, when not tempo synced.
    float hz = 1.0f;

    /// A ModRateType ordinal, when tempo synced. Persisted as-is because
    /// it lands in project files via the Rate lane (see ModInfo.hpp).
    int rateType = static_cast<int>(magda::ModRateType::Hertz);
};

/**
 * @brief What the model says one LFO is.
 *
 * Flat and copyable: rides in the published table. The drawn curve rides
 * beside it in the table's arena, not here.
 */
struct LfoSettings {
    magda::LFOWaveform wave = magda::LFOWaveform::Sine;

    /// Shape behind a Custom waveform with nothing drawn yet.
    magda::CurvePreset preset = magda::CurvePreset::Triangle;

    ModSync sync = ModSync::Free;

    /**
     * @brief What the model says triggers this, before the fold into @ref sync.
     *
     * Carried separately because MIDI and audio both fold to ModSync::Note
     * but gate differently; a mode change is the one edit that must reach a
     * running LFO's gate (LfoState::gated).
     */
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;

    /// Whether rate is a musical division rather than a frequency.
    bool tempoSync = false;

    LfoRate rate;

    /// Added to the phase before the shape is read, 0 to 1.
    float phaseOffset = 0.0f;

    /// Play one cycle and hold the end value. A sustain loop overrides this.
    bool oneShot = false;

    /**
     * @brief Whether the drawn curve is a level rather than an amount.
     *
     * An inactive modifier outputs 0 by convention, so a curve drawn as an
     * audible level (e.g. Sidechain) is applied as its complement.
     */
    bool invertOutput = false;

    /// The sustain loop of a drawn curve: [0, loopStart) plays once,
    /// [loopStart, loopEnd] repeats.
    bool useLoopRegion = false;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;

    /// True for a cross-track sidechain LFO: it ignores triggers from the
    /// track it modulates. See docs/architecture/modifier-lfo.md.
    bool skipNativeResync = false;

    /// Whether triggers gate the output as well as restarting the phase
    /// (note-triggered LFOs behave like envelopes). Off for cross-track LFOs.
    bool gateOnTrigger = false;

    /// Whether the LFO starts with its gate shut.
    bool startGated = false;
};

/**
 * @brief Where one LFO has got to.
 *
 * Owned by the runtime and carried across publishes and plan swaps, so a
 * knob move or link edit leaves phase alone.
 */
struct LfoState {
    /// Cycles since the run began, fractional and monotonic. Fractional
    /// part is the phase; a transport-locked LFO derives this from the
    /// timeline.
    double cycles = 0.0;

    /// What the last block published, for the UI and read-back taps (#2122).
    float phase = 0.0f;
    float value = 0.0f;

    /// Output held at zero; phase keeps advancing underneath, so ungating
    /// resumes where the LFO would have been.
    bool gated = false;

    /// A one-shot that has played through and is holding its end value.
    bool completed = false;

    /// See docs/architecture/modifier-lfo.md ("forceZero").
    bool forceZero = false;

    /// Notes holding the gate open; the gate shuts when the last one lifts.
    int heldNotes = 0;

    /// Whether this state has run at all. A fresh LFO takes its gate from
    /// LfoSettings::startGated on the first block only.
    bool started = false;

    /// The trigger mode this state's gate was seeded under; a mode change
    /// retires the gate rather than leaving it stuck under the old mode.
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;
};

/**
 * @brief Sample rate, tempo and signature a block opens on.
 *
 * For advancing a modifier's own ramp independent of the timeline.
 */
struct ModTiming {
    double sampleRate = 44100.0;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
};

/** @brief The tempo and signature @p block opens on, read off its map. */
ModTiming modTimingFor(const BlockInfo& block, double sampleRate);

/**
 * @brief Where @p block opens, in bars (bar-grid based, not beats / bar-length).
 *
 * See docs/architecture/modifier-lfo.md for why signature changes require
 * this instead of simple division.
 */
double modBarPosition(const BlockInfo& block, const ModTiming& timing);

/** @brief How long a bar is, in beats, under one signature: numerator * 4 / denominator. */
double barBeatsOf(int numerator, int denominator);

/**
 * @brief How many bars @p block covers, for a modifier advancing its own ramp.
 *
 * Handles blocks that span a tempo or signature change (#2340); see
 * docs/architecture/modifier-lfo.md.
 */
double modBarsElapsed(const BlockInfo& block, const ModTiming& timing);

/**
 * @brief How much of a bar one rate type is (ModifierCommon::getBarFraction parity table).
 *
 * Fractions of a bar, not of a whole note.
 */
double barFractionOf(int rateType);

/**
 * @brief How many beats one cycle of a tempo-synced LFO lasts.
 *
 * Matches the TE modifiers' corrected bar arithmetic rather than the
 * reverse (#2128).
 */
double cycleBeats(int rateType, int numerator, int denominator);

/**
 * @brief The rate ordinal a Rate lane's value stands for, and back again.
 *
 * See docs/architecture/modifier-lfo.md ("Rate lane indexing").
 */
int rateTypeFromLaneValue(float laneValue);
float laneValueFromRateType(int rateType);

/**
 * @brief The level @p settings has at @p phase, before gating and inversion.
 *
 * The model's own reading of the shape (core/ModCurve.hpp).
 */
float lfoShapeAt(const LfoSettings& settings, std::span<const magda::CurvePointData> curve,
                 float phase);

/**
 * @brief Restart @p state as a trigger does.
 *
 * Phase to zero, and a finished one-shot plays again. Free-running and
 * transport-locked LFOs are not restarted; callers should not invoke this
 * unless sync == ModSync::Note.
 */
void restartLfo(LfoState& state, const LfoSettings& settings);

/**
 * @brief Advance @p state over one block and publish what @p settings says.
 *
 * Call on the audio thread, once per block, before anything reads the
 * modifier. Returns 0 to 1, inverted where the curve is a level, flat 0
 * while gated.
 */
float advanceLfo(LfoState& state, const LfoSettings& settings,
                 std::span<const magda::CurvePointData> curve, const BlockInfo& block,
                 const ModTiming& timing);

}  // namespace magda::engine
