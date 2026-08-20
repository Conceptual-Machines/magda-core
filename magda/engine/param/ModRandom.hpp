#pragma once

#include <cstdint>

#include "core/ModInfo.hpp"
#include "exec/RenderContext.hpp"
#include "param/ModLfo.hpp"

/**
 * @file ModRandom.hpp
 * @brief The random modulator: a walk that takes one step per cycle, and what
 *        it does between steps.
 *
 * The LFO's arrangement again, and the LFO's timing exactly: rate in Hertz or
 * as a musical division, free-running, timeline-locked or trigger-restarted,
 * one value per block read at the block's first sample. What is different is
 * only what sits on the phase. An LFO reads a shape at it; this reads nothing,
 * and instead uses the phase wrapping as the moment to pick a new number.
 *
 * The walk is bounded rather than uniform: each step lands within
 * @ref RandomSettings::stepDepth of the one before it, so a small depth wanders
 * and a full one jumps anywhere in range. That is the fork's own rule and it is
 * what makes the control mean "how far it can move" rather than "how loud".
 *
 * Depth and polarity stay per link, as everywhere else in this system. The
 * fork's own depth and bipolar parameters are held at unity and off for the
 * same reason (applyRandomProperties), which is why the walk here runs in
 * 0 to 1 and a link is what turns it into a swing about a centre.
 */

namespace magda::engine {

/** @brief What a random modulator does with its cycle. */
enum class RandomShape : std::uint8_t {
    /// A new number each time the cycle wraps, held or ramped across the step
    /// according to @ref RandomSettings::shape. The rate is the step rate.
    Stepped,

    /// A new number every block. The walk and its bounds are the stepped one's
    /// and the rate stops meaning anything, because there is no longer a step
    /// for a rate to be the length of. Shape and smoothing go with it: both
    /// describe what happens between one number and the next, and noise has no
    /// between.
    ///
    /// The fork carries this choice and does nothing with it: its type
    /// parameter is written and never read, so a project set to noise sounds
    /// stepped there. Implemented rather than reproduced as the same omission,
    /// because the model offers the control, and a control that does nothing is
    /// worse than a difference that is written down.
    Noise,
};

/** @brief What the model says one random modulator is. */
struct RandomSettings {
    RandomShape type = RandomShape::Stepped;

    /// Where the phase comes from: the LFO's fold, unchanged, because the model
    /// drives both from the same trigger-mode and tempo-sync fields.
    ModSync sync = ModSync::Free;

    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;
    bool tempoSync = false;
    LfoRate rate;

    /**
     * @brief How much of each step is spent travelling, 0 to 1.
     *
     * Zero holds the new value for the whole step, which is a sample-and-hold.
     * One ramps from the previous value to the new one across the whole of it.
     * In between, the step holds and then ramps over the last @ref shape of
     * itself, which is the fork's own reading of the control.
     */
    float shape = 0.0f;

    /// How much the phase ramp is bent towards an S-curve before anything reads
    /// it, 0 to 1. Applied to the phase rather than to the output, so it eases
    /// the ends of a ramped step and does nothing at all to a held one.
    float smooth = 0.0f;

    /// How far one step can move from the last, 0 to 1. The half-range of the
    /// window the next value is drawn from, clipped to the output range, so a
    /// walk near an end has less room on that side and the same amount on the
    /// other.
    float stepDepth = 1.0f;

    /// Whether this modulator ignores triggers from where it lives, on the
    /// LFO's terms.
    bool skipNativeResync = false;
};

/**
 * @brief Where one random modulator has got to.
 *
 * No gate here, and its absence is deliberate. The LFO and the envelope both
 * have one that a trigger can shut; the fork's random modifier resyncs on a
 * note and has no gate parameter at all, so an audio-triggered one keeps
 * walking between hits rather than resting at zero. A gate added here would be
 * a difference in every project that has one, so a trigger restarts this and
 * nothing shuts it.
 */
struct RandomState {
    /// Cycles since the run began, fractional and monotonic, exactly as the
    /// LFO's: the step boundary is where its fractional part wraps.
    double cycles = 0.0;

    /// The phase the step was read at, after smoothing. Published for the
    /// editor's dot, and compared against to find the wrap.
    float phase = 0.0f;

    float value = 0.0f;

    /// The two ends of the current step. A ramped step travels from the first
    /// to the second; a held one sits on the second.
    float previous = 0.5f;
    float current = 0.5f;

    /// A restart asked for a step and the block has not happened yet. The
    /// trigger arrives between blocks, and taking the step where it lands would
    /// draw a number that the resolve about to run has no way of reaching: the
    /// step boundary is a thing blocks have, so the request waits for one.
    bool stepPending = false;

    /**
     * @brief The state of the walk's own generator.
     *
     * Its own rather than a shared one, so two random modulators in a project
     * are independent and one project renders the same way twice. The fork
     * seeds from the clock and cannot promise either, which is why a parity
     * case here compares the structure of the walk rather than its numbers.
     */
    std::uint64_t seed = 0;
};

/**
 * @brief Seed @p state's generator from the address of the modifier holding it.
 *
 * Off the audio thread, when the state is created. From the address rather than
 * from a counter, so the walk a modifier takes is the same one every time the
 * session opens: a project that renders differently on the second run because
 * its modifiers were prepared in another order is not reproducible.
 */
void seedRandom(RandomState& state, std::uint64_t address);

/**
 * @brief Restart @p state, as a trigger does.
 *
 * The phase goes back to the top, which takes the next step immediately.
 * Refused when the modulator's phase is not its own to restart, on the LFO's
 * terms: a free-running or timeline-locked one ignores the notes going past it.
 */
void restartRandom(RandomState& state, const RandomSettings& settings);

/**
 * @brief Advance @p state over one block and publish what @p settings says.
 *
 * On the audio thread, once per block. Returns the output, 0 to 1. The value
 * published is the one the block opens on and the ramp is moved on afterwards,
 * which is the LFO's order and the fork's.
 */
float advanceRandom(RandomState& state, const RandomSettings& settings, const BlockInfo& block,
                    const ModTiming& timing);

}  // namespace magda::engine
