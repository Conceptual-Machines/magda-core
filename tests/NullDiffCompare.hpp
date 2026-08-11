#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/**
 * @file NullDiffCompare.hpp
 * @brief What "the same" means when two engines render one project (#2040).
 *
 * Nothing here knows about either engine. It is handed two buffers, or two
 * lists of MIDI, and says how far apart they are and where. That is deliberate:
 * this is the one piece of the corpus nothing else checks, and a comparator
 * that passes everything is a worse outcome than an engine that fails, because
 * it would report parity it never measured. So it lives on its own, in the
 * model-only target, with tests that feed it known-bad pairs.
 *
 * Three ideas are worth reading before the declarations.
 *
 * **The floor is arithmetic, not taste.** Two implementations doing the same
 * work in a different order differ by an ulp or two, which is around -140 dBFS
 * at these levels. -120 dBFS leaves room for the order of a summation and no
 * room for anything audible.
 *
 * **One shift per case, measured at the start.** The fork primes its stretcher
 * with the material at a clip's start rather than before it, so its stretched
 * clips begin about a window late, and comparing without allowing for that
 * would only ever measure that. The allowance is a single number found at the
 * start of the material and applied to the whole case. Re-fitting a shift per
 * region would turn a clip that drifts apart into a clip that passes, which is
 * exactly the bug an auto-tempo case exists to catch.
 *
 * **Controllers are compared as a curve against a sampling of it.** The engine
 * emits a controller message on every change of the quantised value, so its
 * step function is the quantised curve exactly; the fork emits on a 1/16-beat
 * grid, so its function is that curve sampled and held. Comparing the two
 * instant for instant is false by construction and gets worse the faster the
 * curve moves, which is why "agree at every instant, within a grid step either
 * way" is not the test: over a pitch-bend dive the fork sends three messages to
 * the engine's hundred, and at most instants it simply has not sent the value
 * the curve is at.
 *
 * What is asserted instead is that the sampling is faithful: every message the
 * fork sends lands on the engine's curve within one grid step, and the two
 * cover the same span. A denser stream carrying the same values passes. A wrong
 * value, a curve with the wrong tension, a stream that arrives late and one
 * that stops early all fail. Equal extremes are deliberately not asserted, for
 * a reason worth reading in the implementation: a grid sampler misses the peak
 * of anything moving faster than its grid, and missing it is the divergence
 * rather than evidence of one.
 */

namespace magda::nulldiff {

// =============================================================================
// Audio
// =============================================================================

struct AudioCompareOptions {
    /// Peak residual at or below this is a null. Cases may raise it, and a
    /// raised floor without a mechanism beside it does not belong in the
    /// corpus.
    double floorDb = -120.0;

    /// Look for a fixed offset between the two before comparing. Only for cases
    /// that declare one, because a shift the corpus did not expect is a clip in
    /// the wrong place rather than an alignment to be undone.
    bool measureShift = false;

    /// How far the search may look, in samples. A stretcher's priming is a
    /// window or two; anything beyond this is not priming.
    int maxShiftSamples = 8192;

    double sampleRate = 44100.0;
};

struct AudioComparison {
    /// Peak and RMS of the residual, in dBFS. -inf when the two are identical.
    double peakDb = -std::numeric_limits<double>::infinity();
    double rmsDb = -std::numeric_limits<double>::infinity();

    /// First sample past the floor, or -1. In the aligned domain, which is to
    /// say an index into the native render.
    std::int64_t firstDivergence = -1;

    /// Samples the incumbent was found to be late by. Zero unless the case
    /// asked for a shift and one was found.
    int shiftSamples = 0;

    /// True when a shift was asked for and the search declined to name one,
    /// which is what a pair that differs in content rather than in timing gets.
    /// The comparison still runs, unaligned, and fails on its merits.
    bool shiftNotFound = false;

    std::int64_t comparedSamples = 0;

    /// The two renders were not the same length. Reported rather than trimmed
    /// away silently, because a render that came back short is a bug in a leg.
    std::int64_t lengthDifference = 0;

    /// Set when the pair could not be compared at all, which is a different
    /// thing from comparing badly: different channel counts, or nothing to
    /// compare. A refusal is never reported as a residual.
    std::string refusal;

    bool withinFloor() const {
        return peakDb <= floorUsed;
    }

    double floorUsed = -120.0;
};

/**
 * @brief Compare @p native against @p incumbent.
 *
 * The residual is native minus incumbent, per channel, over the samples both
 * cover. A pair with different channel counts is refused rather than compared
 * on the channels they share.
 */
AudioComparison compareAudio(const juce::AudioBuffer<float>& native,
                             const juce::AudioBuffer<float>& incumbent,
                             const AudioCompareOptions& options = {});

/**
 * @brief How many samples @p incumbent lags @p native, or nothing.
 *
 * Normalised cross correlation over a window at the onset of the material,
 * which is where a priming offset lives and where a case that drifts has not
 * drifted yet. Returns nullopt when the best lag does not correlate well
 * enough to be called an alignment: a comparator that slides until something
 * matches will always find something, and what it finds on a pair that differs
 * in content is a fiction that makes the case pass.
 */
struct ShiftEstimate {
    int samples = 0;
    double correlation = 0.0;
    bool found = false;
};

ShiftEstimate estimateShift(const juce::AudioBuffer<float>& native,
                            const juce::AudioBuffer<float>& incumbent, int maxShiftSamples);

// =============================================================================
// MIDI
// =============================================================================

/// One short message, positioned on the timeline rather than within a block.
/// Both legs tap the instrument's input, which is the only point that means
/// anything: what a synth receives.
struct MidiEvent {
    std::int64_t sample = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    int channel() const {
        return (status & 0x0F) + 1;
    }
    int type() const {
        return status & 0xF0;
    }
};

using MidiStream = std::vector<MidiEvent>;

/// A note as a lifetime rather than as two messages, which is the form the
/// invariant is stated in and therefore the form to compare.
struct MidiNoteSpan {
    int channel = 0;
    int pitch = 0;
    int velocity = 0;
    std::int64_t onSample = 0;
    std::int64_t offSample = 0;
};

struct MidiLifetime {
    std::vector<MidiNoteSpan> notes;

    /// A note-off for a note that never started, and a note that never ended.
    /// Checked on each stream on its own before the two are compared: a note
    /// left hanging by the incumbent is not a reason to accept one here.
    int unmatchedOffs = 0;
    int hanging = 0;
};

/// Pair note-ons with their offs. A note-on of velocity 0 is an off, as it is
/// everywhere else in MIDI.
MidiLifetime pairNotes(const MidiStream& stream);

struct MidiCompareOptions {
    double sampleRate = 44100.0;

    /// Used to turn the fork's 1/16-beat grid into a duration. The slack is
    /// what the divergence is, so it is expressed in the fork's own units.
    double bpm = 120.0;
    double staleBeats = 1.0 / 16.0;

    /// Samples the incumbent's notes are expected to be late by, declared by
    /// the case. Non-zero for exactly one thing: the fork drops midiOffset on
    /// an unlooped arranger clip, so every note of such a clip lands offset.
    /// Declared and asserted, never fitted.
    std::int64_t noteShiftSamples = 0;

    /// Two engines round a beat to a sample through different arithmetic.
    int noteToleranceSamples = 1;

    /// Samples the incumbent ends every note early by, declared by the case.
    ///
    /// The fork nudges every note-off backwards by 0.0001 s "to make sure the
    /// ordering is correct" (MidiNote::getPlaybackTime), which is its way of
    /// keeping an off ahead of an on at the same instant. The engine orders
    /// events explicitly at compile time instead, so it keeps the length the
    /// note was drawn at. Pinned here rather than absorbed into the tolerance
    /// above: that tolerance is for a sample of rounding, and a note 100
    /// microseconds short is a different claim about a different thing.
    int incumbentNoteEndEarlySamples = 0;

    /// A controller is seven bits and pitch bend fourteen, so this is the
    /// resolution the value is transmitted at.
    int valueTolerance = 1;
};

struct MidiComparison {
    bool notesMatch = false;
    bool controllersMatch = false;

    int notesCompared = 0;
    int notesOnlyInNative = 0;
    int notesOnlyInIncumbent = 0;
    int notesMismatched = 0;

    int nativeHanging = 0;
    int incumbentHanging = 0;

    /// Messages that are neither notes, controllers nor pitch bend, per side.
    ///
    /// Counted rather than skipped. Everything this comparator does not
    /// understand is something it would otherwise drop in silence, and a
    /// difference nobody counted is indistinguishable from no difference: the
    /// fork sends channel pressure with every MPE note, and ignoring it quietly
    /// is how that would have gone unnoticed.
    int nativeUncompared = 0;
    int incumbentUncompared = 0;

    /// Every disagreement, addressed: which note, which controller, which
    /// instant. A parity failure that says "the streams differ" is a day of
    /// somebody's life.
    std::vector<std::string> problems;

    bool passed() const {
        return notesMatch && controllersMatch;
    }
};

/**
 * @brief Compare two captured MIDI streams.
 *
 * Notes exactly, to a sample either way. Channels as assigned rather than
 * canonicalised to order of first use: the fork's MPE round-robin was ported
 * deliberately, so a channel that differs is a finding, and canonicalising
 * would be the corpus deciding not to look.
 *
 * Controllers and pitch bend as step functions of time, agreeing to one
 * quantised unit at some instant within one grid step either way, in both
 * directions.
 */
MidiComparison compareMidi(const MidiStream& native, const MidiStream& incumbent,
                           const MidiCompareOptions& options = {});

// =============================================================================
// The report
// =============================================================================

/// What a case claims. The corpus declares it; the runner asserts it.
enum class Verdict {
    /// Sample for sample, with nothing allowed for.
    Null,

    /// Sample for sample after one pinned shift, measured at the start and
    /// asserted against what the engine predicts.
    Shift,

    /// No audio assertion. The MIDI channel carries the case.
    Midi,

    /// Measured and printed, asserted only to be finite. One case, which exists
    /// to say how far apart the two stretchers are on broadband material.
    ReportOnly,
};

const char* toString(Verdict verdict);

struct CaseReport {
    std::string name;
    Verdict verdict = Verdict::Null;

    bool hasAudio = false;
    AudioComparison audio;

    bool hasMidi = false;
    MidiComparison midi;

    /// Set when the case could not be measured at all: a proxy that never
    /// arrived, a leg that returned nothing. Never reported as a residual,
    /// because a race reported as a parity failure costs somebody a day inside
    /// the engine looking for it.
    std::string unmeasurable;

    bool passed = false;
};

/**
 * @brief The corpus as canonical text, printed on every run.
 *
 * Numbers that move are the point. A corpus that only prints when it fails
 * cannot show a residual creeping from -138 dB to -122 dB, which is what an
 * engine going subtly wrong looks like before it goes audibly wrong.
 */
std::string formatReport(const std::vector<CaseReport>& cases, double sampleRate, int blockSize);

/// dBFS from a linear amplitude, with a printable answer for silence.
std::string formatDb(double db);

}  // namespace magda::nulldiff
