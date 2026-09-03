#pragma once

#include <cstdint>

#include "core/ModInfo.hpp"
#include "exec/RenderContext.hpp"
#include "param/ModLfo.hpp"

/**
 * @file ModAdsr.hpp
 * @brief The envelope generator: what it is, what stage it is in, and how far a
 *        block moves it.
 *
 * The LFO's three pieces, in the same arrangement and for the same reasons
 * (ModLfo.hpp says them at length). AdsrSettings is what the model says the
 * envelope is and rides in the published table; AdsrState is where it has got
 * to and belongs to the runtime; advanceAdsr is one block of the first applied
 * to the second.
 *
 * Two things differ from the LFO, and both are the fork's rather than choices
 * made here.
 *
 * The first is the gate. An LFO's trigger restarts a phase and a gate is
 * something extra a note-triggered one opts into; an envelope is nothing but a
 * gate, and its whole shape is the answer to how long the gate has been open
 * and how long ago it shut. So the gate is not optional here, and where it
 * comes from is what the sync mode selects: a free-running envelope holds its
 * gate open and cycles, a transport-locked one is gated by playback, and a
 * note-driven one is gated by whatever is feeding it notes.
 *
 * The second is when the value is read. The fork's LFO timer publishes the
 * phase the block opens on and then moves the ramp; its ADSR timer advances
 * first and publishes where the block ends up. The two are one block apart and
 * this reproduces the second, because a parity case is measured against the
 * fork and not against the tidier of its two conventions.
 *
 * What is deliberately not here, exactly as with the LFO, is depth and
 * polarity: one envelope drives several parameters by different amounts, and
 * MAGDA keeps that per link.
 */

namespace magda::engine {

/**
 * @brief Which part of the envelope is running.
 *
 * The ordinals are te::ADSRModifier::Stage's, because the model carries this
 * back to the UI as an ordinal (ModInfo::envStage) and the stage readout is
 * shared with the fork. A display that renumbered them would light the wrong
 * segment in one of the two engines.
 */
enum class AdsrStage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

/**
 * @brief What the model says one envelope is.
 *
 * Flat and copyable, riding in the published table beside the LFO's.
 */
struct AdsrSettings {
    /// Stage lengths in milliseconds, which is what the model stores and what a
    /// modifier that is not tempo synced runs at.
    float attackMs = 10.0f;
    float decayMs = 200.0f;
    float releaseMs = 300.0f;

    /// The level held while the gate stays open, 0 to 1.
    float sustain = 0.7f;

    /// Per-segment curvature, -0.5 to 0.5: a quadratic bezier bent from
    /// logarithmic through linear at zero to exponential. The fork's own
    /// control point arithmetic, because the numbers land in project files.
    float attackCurve = 0.0f;
    float decayCurve = 0.0f;
    float releaseCurve = 0.0f;

    /**
     * @brief Where the gate comes from.
     *
     * Folded from the trigger mode alone. Unlike the LFO's, tempo sync is not
     * part of this fold: for an LFO it decides what the period is and therefore
     * whether the phase is a function of the timeline, and for an envelope it
     * only scales the stage lengths. A tempo-synced free-running envelope is
     * still free-running (ModifierHelpers::mapADSRSyncType).
     */
    ModSync sync = ModSync::Free;

    /// What the model says triggers it, carried for the reason the LFO carries
    /// it: MIDI and audio both run as ModSync::Note and gate differently, so a
    /// mode change has to be visible to a running gate.
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;

    /// Whether the stage lengths are musical divisions rather than
    /// milliseconds. Which unit the envelope runs in, not only how its lengths
    /// are worked out: a synced one advances by the beats a block covered.
    bool tempoSync = false;

    /**
     * @brief The division every stage runs at when tempo synced.
     *
     * One for all three, because that is what the model carries: the fork
     * writes it to the ADSR's separate attack, decay and release sync
     * parameters and they are always the same value (applyADSRProperties).
     */
    int rateType = static_cast<int>(magda::ModRateType::Hertz);

    /// Whether this envelope ignores triggers from where it lives, so a
    /// cross-track one is driven by its source alone. The LFO's flag, with the
    /// same meaning and the same reason.
    bool skipNativeResync = false;

    /// Whether it starts with its gate shut: what an audio-triggered envelope
    /// is between hits, and a note-triggered one before the first note.
    bool startGated = false;
};

/** @brief Where one envelope has got to. */
struct AdsrState {
    AdsrStage stage = AdsrStage::Idle;

    /// How far the current stage has run, in whatever its length is counted in:
    /// seconds for an envelope whose stages are milliseconds, beats for one
    /// whose stages are a musical division (#2340).
    double timeInStage = 0.0;

    /// The level the stage started from, so a gate change is click-free from
    /// wherever the envelope happened to be rather than from the top.
    float stageStartValue = 0.0f;

    /// How far through the current stage, 0 to 1, for the display. Zero in the
    /// stages that have no length: idle and sustain.
    float stagePhase = 0.0f;

    float value = 0.0f;

    /// The gate as the triggers left it. Shut means shut: the sync mode
    /// decides whether this is what is read at all.
    bool gated = false;

    /// The gate this state last saw, which is what makes a change an edge. A
    /// gate that is merely open is not a retrigger.
    bool lastGate = false;

    /// A trigger has asked for one block of nothing, on the LFO's terms and
    /// for its reason (LfoState::forceZero): the trigger lands inside a block
    /// whose parameters are already resolved, so the gap it stands for is
    /// spent on the next one, and the attack starts from zero after it.
    bool forceZero = false;

    /// Notes holding the gate open.
    int heldNotes = 0;

    bool started = false;

    /// The trigger mode this state's gate was seeded under, so a mode change
    /// retires the gate the old mode left. The LFO's rule, and an envelope
    /// needs it more: one left shut by a mode nothing triggers any more is an
    /// envelope that never sounds again.
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;
};

/**
 * @brief How long one stage lasts, in seconds.
 *
 * The milliseconds the model stores, or the musical division they are replaced
 * by when the envelope is tempo synced. A division of Hertz is not a division,
 * and falls back to the milliseconds, which is the fork's own rule.
 *
 * What a synced envelope actually runs on is that division in beats, because
 * the block says how many beats it covered and a bpm read once per block does
 * not survive a tempo change inside it (#2340). This is the same length said in
 * seconds, which is what the millisecond path needs and what a reader asking
 * how long a stage is wants to hear.
 */
double adsrStageSeconds(float milliseconds, const AdsrSettings& settings, const ModTiming& timing);

/**
 * @brief The level a curved segment has @p t of the way through it.
 *
 * The fork's quadratic bezier (BezierHelpers), because the curvature lands in
 * project files as a number between -0.5 and 0.5 and the shape it stands for
 * has to be the same one in both engines. A curvature of zero is the straight
 * line, taken early rather than solved for.
 *
 * Exposed because the tests want the shape without the run.
 */
float adsrSegmentAt(float t, float startValue, float endValue, float curve);

/**
 * @brief Restart @p state, as a trigger does.
 *
 * Into the attack, from where the envelope already was, which is what makes a
 * retrigger click-free from any stage. @p fromZero starts it from silence
 * instead, which is what a cross-track trigger asks for so the device sees a
 * transition rather than a continuation.
 *
 * Unlike the LFO's restart this acts in every sync mode. An LFO that is not
 * listening for triggers has a phase of its own that a trigger has no business
 * moving; an envelope that is not listening has no gate anything else can open,
 * so a trigger reaching it is the only thing that could have.
 */
void restartAdsr(AdsrState& state, const AdsrSettings& settings, bool fromZero);

/**
 * @brief Advance @p state over one block and publish what @p settings says.
 *
 * On the audio thread, once per block, before anything reads the modifier.
 * Returns the output, 0 to 1, and 0 is an envelope at rest, which is the
 * convention the whole modulation system reads as a modifier doing nothing.
 *
 * The gate is settled first, from the sync mode: open always while free
 * running, from @p block while transport locked, and from the state's own gate
 * while note driven. A change of it is an edge, and an edge is what enters the
 * attack or the release.
 */
float advanceAdsr(AdsrState& state, const AdsrSettings& settings, const BlockInfo& block,
                  const ModTiming& timing);

}  // namespace magda::engine
