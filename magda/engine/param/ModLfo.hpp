#pragma once

#include <cstdint>
#include <span>

#include "core/ModInfo.hpp"
#include "exec/RenderContext.hpp"

/**
 * @file ModLfo.hpp
 * @brief The LFO: what it is, where it has got to, and how far a block moves it.
 *
 * A modifier was a number until now. Slice 2 gave it an identity so a link
 * could name one, and the value it published was whatever the model last saw,
 * which is a value that does not move between publishes. This is the engine
 * that makes it move.
 *
 * Three pieces, and they are separate on purpose. LfoSettings is what the
 * model says the LFO is, which travels in the published table and is immutable
 * for as long as that table is. LfoState is where the LFO has got to, which
 * belongs to the runtime rather than to the table: a knob move republishes the
 * table many times a second and an LFO that restarted on each of them would
 * never finish a cycle. advanceLfo is one block of the first applied to the
 * second.
 *
 * Block rate, once per block, from the block's first sample, because that is
 * what the incumbent does: TE advances a modifier timer at the top of the
 * block and every plugin reading it that block reads one value. A modifier
 * that ran per sample would differ from the fork on every modulated parameter
 * in every project, which is a decision for after the port.
 *
 * What is deliberately not here is depth and polarity. One LFO drives several
 * parameters by different amounts and in different directions, and MAGDA keeps
 * that per link; it is applied where the contributions are summed
 * (ParamResolve.hpp) and the LFO knows nothing about it. The incumbent holds
 * TE's own depth, offset and bipolar parameters at unity for the same reason.
 */

namespace magda::engine {

/**
 * @brief What drives a modifier's output.
 *
 * Only the LFO runs during this slice. The rest are named so a table can
 * describe a modifier the engine cannot yet advance, which reads its model
 * value and holds it, exactly as every modifier did before this slice; they
 * arrive in #2120.
 */
enum class ModKind : std::uint8_t { Lfo, Adsr, Random, Follower };

/**
 * @brief Where a modifier's phase comes from.
 *
 * The model says trigger mode and tempo sync separately, and the fork folds
 * the two into one choice (ModifierHelpers::mapSyncType). Folded here as well,
 * and for the same reason: what actually differs is whether the phase is a
 * function of the timeline, a ramp of its own, or a ramp of its own that
 * something restarts.
 */
enum class ModSync : std::uint8_t {
    /// A ramp of its own, advanced by however long the block was. Keeps
    /// running with the transport stopped, because the graph is still
    /// processing and the LFO is still moving.
    Free,

    /// A function of where the timeline is, so two LFOs at one rate are in
    /// phase with each other and with the bar however playback got there.
    /// Standing still while the transport is stopped is the same statement.
    Transport,

    /// A ramp of its own, restarted by a trigger. What a note-triggered LFO
    /// is, and what a cross-track sidechain LFO is.
    Note,
};

/** @brief The rate of one modifier, as the block reads it. */
struct LfoRate {
    /// Cycles per second, when the modifier is not tempo synced.
    float hz = 1.0f;

    /// A ModRateType ordinal, when it is. The persisted ordinal rather than a
    /// division of MAGDA's own, because it is what lands in project files
    /// through the Rate lane, so the divisions are a mapping rather than a
    /// decision (ModInfo.hpp says so at the enum).
    int rateType = static_cast<int>(magda::ModRateType::Hertz);
};

/**
 * @brief What the model says one LFO is.
 *
 * Flat and copyable: it rides in the published table, and the drawn curve
 * rides beside it in the table's own arena rather than in here, because a
 * vector per modifier would be an allocation per modifier.
 */
struct LfoSettings {
    magda::LFOWaveform wave = magda::LFOWaveform::Sine;

    /// The shape behind a Custom waveform with nothing drawn on it yet.
    magda::CurvePreset preset = magda::CurvePreset::Triangle;

    ModSync sync = ModSync::Free;

    /**
     * @brief What the model says triggers it, before the fold into @ref sync.
     *
     * Carried as well as folded, because the fold is lossy in the one place it
     * matters: MIDI and audio both run as ModSync::Note and gate differently,
     * so the gate cannot be reconciled against the folded form. A mode change
     * is the one edit that has to reach a running LFO's gate (LfoState::gated).
     */
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;

    /// Whether the rate is a musical division rather than a frequency. What
    /// the Rate parameter means depends on it, which is why the two travel
    /// together.
    bool tempoSync = false;

    LfoRate rate;

    /// Added to the phase before the shape is read, 0 to 1.
    float phaseOffset = 0.0f;

    /// Play one cycle and hold what it ended on. A sustain loop overrides it:
    /// the region repeats rather than the cycle finishing.
    bool oneShot = false;

    /**
     * @brief Whether the drawn curve is a level rather than an amount.
     *
     * An inactive modifier outputs 0, which is the engine convention every
     * gate and every untriggered LFO relies on. A curve drawn as an audible
     * level therefore has to be applied as its complement, or idle would read
     * as silence: the Sidechain device draws the gain shape the user hears and
     * the engine receives the duck.
     */
    bool invertOutput = false;

    /// The sustain loop of a drawn curve: the intro [0, loopStart) plays once
    /// and [loopStart, loopEnd] repeats from there.
    bool useLoopRegion = false;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;

    /**
     * @brief Whether this LFO ignores triggers from where it lives.
     *
     * A cross-track sidechain LFO is driven by the source track alone: the
     * track it modulates must not retrigger its phase or slam its gate on
     * every note it happens to be playing. The fork learned this the hard way,
     * with a flag that was set at creation and not restated when a source was
     * picked later, so the destination track went on retriggering an LFO that
     * had stopped being its own.
     */
    bool skipNativeResync = false;

    /**
     * @brief Whether triggers gate the output as well as restarting the phase.
     *
     * A note-triggered LFO behaves like an envelope: silent between notes,
     * running while one is held. Off for a cross-track LFO, whose idle state
     * is the one-shot hold or a free-running loop rather than nothing at all.
     */
    bool gateOnTrigger = false;

    /// Whether the LFO starts with its gate shut. What an audio-triggered LFO
    /// is between hits, and what a note-triggered one is before the first note.
    bool startGated = false;
};

/**
 * @brief Where one LFO has got to.
 *
 * Owned by the runtime and carried across publishes and plan swaps, so a knob
 * move, a link edit or a device insert leaves the phase where it was.
 */
struct LfoState {
    /**
     * @brief Cycles since the run began, fractional and monotonic.
     *
     * The phase is its fractional part, and everything that needs to know how
     * far through the LFO is rather than where in the cycle it is asks this:
     * a one-shot is through when it reaches one, and a sustain loop folds it
     * back into the region. A transport-locked LFO derives it from the
     * timeline instead of accumulating it.
     */
    double cycles = 0.0;

    /// What the last block published, for the UI and for the read-back taps
    /// (#2122). The phase is the position the shape was read at, so a looped
    /// curve reports where in the loop it is rather than the raw sweep.
    float phase = 0.0f;
    float value = 0.0f;

    /// Output held at zero. Not a pause: the phase goes on advancing
    /// underneath, so ungating resumes where the LFO would have been rather
    /// than where it was when the gate shut.
    bool gated = false;

    /// A one-shot that has played through and is holding its end value.
    bool completed = false;

    /**
     * @brief A trigger has asked for one block of nothing.
     *
     * The gap a gated retrigger would have left, so a device sees a transition
     * rather than a continuation. Latched rather than written straight to the
     * output, because a trigger arrives inside a block whose parameters have
     * already been resolved: the block after it is the first one a device can
     * see, and a value written at the moment of the trigger would be
     * overwritten before anything read it.
     *
     * The block that spends it holds the phase as well, so the shape resumes
     * from its start on the block after. That is the fork's own sequence, one
     * block later, which is the same block the trigger itself is late by.
     */
    bool forceZero = false;

    /// Notes holding the gate open. The gate shuts when the last one lifts,
    /// which is what makes a note-triggered LFO read as an envelope.
    int heldNotes = 0;

    /**
     * @brief Whether this state has run at all.
     *
     * A fresh LFO takes its gate from LfoSettings::startGated on its first
     * block; after that the gate belongs to the triggers. The fork re-asserts
     * it from the model on every property update, because a TE modifier's gate
     * cannot be reached from the message thread any other way; here the gate
     * lives beside the phase, so a publish that re-asserted it would be
     * fighting the note that had just opened it.
     */
    bool started = false;

    /**
     * @brief The trigger mode this state's gate was seeded under.
     *
     * The one edit a running gate has to hear. A gate belongs to the triggers
     * of the mode that opened it, and a mode change retires all of them: an
     * audio-triggered LFO sits shut between hits, and switching it to free
     * running would otherwise leave it shut for ever, because nothing in the
     * new mode ever opens a gate.
     *
     * Compared rather than re-applied, so an ordinary publish leaves the gate
     * where the last note put it. The fork gets the same effect by re-applying
     * unconditionally, which it can afford because its gate is not also where
     * a held note is counted.
     */
    magda::LFOTriggerMode trigger = magda::LFOTriggerMode::Free;
};

/**
 * @brief What one block needs to know about time that the block itself cannot say.
 *
 * The sample rate, because a free-running LFO advances by how long the block
 * lasted rather than by how far the timeline moved, and the two are different
 * things while the transport is stopped. The tempo, because a stopped block
 * covers no beats and a free-running modifier still has to keep musical time.
 * And the whole signature, because a musical division is a fraction of a bar
 * and a bar is neither number on its own: a rolling block reads the bars it
 * covers off the map instead (@ref modBarsElapsed), which is what a stopped
 * one has no map to read them from.
 */
struct ModTiming {
    double sampleRate = 44100.0;

    /// The tempo the block opens at. What a stopped block keeps time at, and
    /// nothing else: a rolling block says how many bars it covers, and that is
    /// the map's own answer across a tempo or signature change inside it (@ref
    /// modBarsElapsed).
    double bpm = 120.0;

    /// The signature at the block's first beat. Both halves of it, because a
    /// bar is the numerator counted in the denominator's note value and either
    /// on its own describes the wrong bar.
    int numerator = 4;
    int denominator = 4;
};

/** @brief The tempo and signature @p block opens on, read off its map. */
ModTiming modTimingFor(const BlockInfo& block, double sampleRate);

/**
 * @brief Where @p block opens, counted in bars.
 *
 * The bar grid rather than a beat count divided by a bar's length, because the
 * two part company at a signature change: a change always starts a bar
 * (TempoMap.hpp), so two bars of four four followed by six eight puts a bar
 * line at beat eight, and eight divided by the three beats a six eight bar
 * lasts is two and two thirds. A modifier reading that opens every six eight
 * bar two thirds of the way through its cycle and stays there.
 *
 * Whole bars plus the fraction of this one that has gone by, which restarts at
 * the change and carries the denominator with it, since the beat inside a bar
 * is counted in the signature's own note value.
 *
 * Without a map there are no changes to survive, and the closed form is the
 * same number: a hand-assembled block gets the arithmetic the grid would have
 * given it.
 *
 * Shared because every timeline-locked modifier asks it, not only the LFO: a
 * synced random walk that stepped on a different grid from the LFO beside it
 * would be two answers to one question.
 */
double modBarPosition(const BlockInfo& block, const ModTiming& timing);

/**
 * @brief How long a bar is, in beats, under one signature.
 *
 * The numerator counted in the denominator's own note value:
 * `numerator * 4 / denominator` quarter notes, because a beat is a quarter
 * note here and everywhere else in the engine. A bar of six eight is three of
 * them, not six. Shared between @ref cycleBeats, which asks it of one
 * signature, and @ref modBarsElapsed, which asks it of every signature a
 * block crosses.
 */
double barBeatsOf(int numerator, int denominator);

/**
 * @brief How many bars @p block covers, for a modifier advancing its own ramp.
 *
 * The block's own beat length, translated into bars off the map. Not a single
 * division, because a bar is a different number of beats on each side of a
 * signature change, and a block that spans one covers a fraction of each: the
 * map knows where the change is, so this walks the spans between them and
 * sums what each is worth in its own bar length. Right across a tempo step as
 * well, since @ref modBarsElapsed reads through the same grid the beats
 * themselves came from; block seconds scaled by a single bpm and signature
 * read anywhere in the block is not, and runs the rest of a block that spans a
 * change at the numbers it opened on (#2340).
 *
 * A stopped block is the one that still needs a tempo and a signature. It
 * covers no beats at all, and a free-running modifier goes on turning through
 * it, so there the bars are how long the block lasted at the tempo and
 * signature the cursor is sitting on. A stopped cursor is on one beat, so one
 * bpm and one bar length are exact there.
 *
 * Shared because the LFO, the random walk and the envelope all ask it, and two
 * answers to one question is what a modifier drifting from the one beside it
 * would be.
 */
double modBarsElapsed(const BlockInfo& block, const ModTiming& timing);

/**
 * @brief How much of a bar one rate type is.
 *
 * The fork's own table (ModifierCommon::getBarFraction), because a synced LFO's
 * period has to be the same number of beats in both engines or every synced
 * project drifts. Note that these are fractions of a bar rather than of a whole
 * note: "1/2" is half a bar, which is a half note in four four and three
 * quarters of one in three four.
 */
double barFractionOf(int rateType);

/**
 * @brief How many beats one cycle of a tempo-synced LFO lasts.
 *
 * The bar fraction times the length of a bar, and a bar is the signature's
 * numerator counted in its own note value: `numerator * 4 / denominator`
 * quarter notes, because a beat is a quarter note here and everywhere else in
 * the engine. A bar of six eight is three of them, not six.
 *
 * The fork used to count the numerator in quarter notes, so a "1 Bar" modifier
 * in six eight ran two written bars long and every other division was out by
 * the same ratio. The four TE modifiers are patched to match this rather than
 * this being bent to match them: an engine being replaced is not a reason to
 * carry its arithmetic forward, and the two agreeing is what keeps the
 * null-diff corpus meaningful (#2128).
 */
double cycleBeats(int rateType, int numerator, int denominator);

/**
 * @brief The rate ordinal a Rate lane's value stands for, and back again.
 *
 * The lane counts from the first musical division rather than from the
 * enumeration's own zero, because zero is Hertz and Hertz is not a division: a
 * synced modifier whose lane ran to the bottom of its range would otherwise
 * resolve to "not synced at all". The model's Rate control stores the shifted
 * index (AutomationInfo::makeSyncDivisionInfo) and this is the same shift on
 * the engine's side of the same lane.
 */
int rateTypeFromLaneValue(float laneValue);
float laneValueFromRateType(int rateType);

/**
 * @brief The level @p settings has at @p phase, before gating and inversion.
 *
 * The model's own reading of the shape (core/ModCurve.hpp). Exposed because
 * the read-back taps and the tests both want the shape without the run.
 */
float lfoShapeAt(const LfoSettings& settings, std::span<const magda::CurvePointData> curve,
                 float phase);

/**
 * @brief Restart @p state, as a trigger does.
 *
 * The phase goes back to the top and a finished one-shot plays again. A
 * free-running or timeline-locked LFO is not restarted at all, which is the
 * fork's rule as well: resync only acts when the sync type is note, so an LFO
 * that is not listening for a trigger ignores the notes going past it.
 *
 * Whether a trigger is one this LFO listens to is the caller's question rather
 * than this one's: LfoSettings::skipNativeResync says the LFO belongs to
 * another track's monitor, and that is about where a trigger came from.
 */
void restartLfo(LfoState& state, const LfoSettings& settings);

/**
 * @brief Advance @p state over one block and publish what @p settings says.
 *
 * On the audio thread, once per block, before anything reads the modifier.
 * Returns the output: 0 to 1, inverted where the curve is a level, and 0 flat
 * while the gate is shut, which is the convention that makes an inactive
 * modifier mean "doing nothing" everywhere it is read.
 *
 * The value published is the one the block opens on, and the ramp is moved on
 * afterwards, which is the order the fork's timer uses and therefore the order
 * a parity case is measured against.
 */
float advanceLfo(LfoState& state, const LfoSettings& settings,
                 std::span<const magda::CurvePointData> curve, const BlockInfo& block,
                 const ModTiming& timing);

}  // namespace magda::engine
