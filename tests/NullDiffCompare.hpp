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

    /// A null is the whole render agreeing, not the part both happened to
    /// cover. The residual is measured over the overlap, so a short incumbent
    /// with an identical prefix would otherwise be certified as a null while
    /// being exactly what this file calls a bug in a leg.
    bool nulled() const {
        return refusal.empty() && lengthDifference == 0 && withinFloor();
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

    /// The peak refined below a sample, by a parabola through the correlation
    /// either side of it. A whole-sample answer is not enough for a band
    /// limited tone: at 220 Hz one sample of misalignment is a residual around
    /// -30 dB, so a case can be a full sample out and look like a bug in the
    /// material rather than a bug in the timing.
    double fractionalSamples = 0.0;

    double correlation = 0.0;
    bool found = false;

    /// The same alignment as the envelopes see it, and how well they agree
    /// there. This is the only answer available for stretched material: two
    /// vocoders never correlate as waveforms, so `found` is false for them by
    /// construction, and the envelope is what survives the phase difference.
    /// Resolved to the decimation rather than to a sample, which is ample for
    /// an offset measured in thousands.
    double envelopeSamples = 0.0;
    double envelopeCorrelation = 0.0;
};

ShiftEstimate estimateShift(const juce::AudioBuffer<float>& native,
                            const juce::AudioBuffer<float>& incumbent, int maxShiftSamples);

/**
 * @brief @p source delayed by @p delaySamples, which may be fractional.
 *
 * A windowed sinc, wide enough that its own error sits far below the floor on
 * anything band limited. Deliberately not the engine's four-point cubic: this
 * is used to align a case before measuring what is left, and aligning with the
 * kernel under test would fold the thing being measured into the measurement.
 */
juce::AudioBuffer<float> delayFractionally(const juce::AudioBuffer<float>& source,
                                           double delaySamples);

// =============================================================================
// Stretched audio, where a waveform comparison is the wrong question
// =============================================================================

/**
 * Two vocoders fed the same material do not converge on the same waveform, and
 * no amount of aligning makes them. Priming sets the initial phase state, phase
 * in a vocoder is memory, and the two legs prime differently: the fork with the
 * material at the clip's start, the engine with the material before it. From
 * there the outputs stay a fixed distance apart in phase for ever, even with
 * identical libraries and identical input afterwards.
 *
 * Magnitude is what framing leaves intact, so that is what is compared. But
 * magnitude alone would let a wrong ratio, a misplaced clip or a dropped block
 * through, so a stretched case is three assertions rather than one:
 *
 *  - the pinned shift, measured by cross correlation and asserted against what
 *    the engine says its stretcher primes with;
 *  - envelope timing after that shift, which is what keeps a placement bug
 *    visible when the waveform cannot be compared;
 *  - the magnitude spectrogram, which is what "the same material at the same
 *    rate and the same pitch" means when the phase is allowed to differ.
 */

struct EnvelopeAgreement {
    /// Where the two amplitude envelopes line up best, in samples, refined
    /// below a sample. Zero is what a correctly placed clip gives.
    double lagSamples = 0.0;

    /// How well they line up there. A low peak means the two are not the same
    /// shape at all, which no lag can fix.
    double correlation = 0.0;
};

/// Rectified and smoothed at @p followerHz, then cross correlated. The envelope
/// is what a listener would call the timing of a sound, and it survives the
/// phase difference that makes the waveforms incomparable.
EnvelopeAgreement compareEnvelopes(const juce::AudioBuffer<float>& native,
                                   const juce::AudioBuffer<float>& incumbent, int shiftSamples,
                                   double sampleRate, double followerHz = 40.0);

struct SpectralAgreement {
    /// Per-bin magnitude difference in dB, across every frame and bin loud
    /// enough to mean anything. The median says whether they are the same
    /// sound; the 95th percentile is what a case asserts on, because a
    /// difference concentrated in a few frames is exactly what a dropped block
    /// looks like.
    double medianDb = 0.0;
    double percentile95Db = 0.0;

    int frames = 0;
    int binsCompared = 0;
};

/// Window and hop are stated rather than tuned: 2048 at 44.1 kHz is about 46 ms,
/// long enough to resolve a 220 Hz tone into a few bins, and a quarter-window
/// hop is the usual overlap for a Hann.
constexpr int kSpectralWindow = 2048;
constexpr int kSpectralHop = 512;

/// Magnitude spectrogram agreement. Bins below @p floorDb of the frame's own
/// peak are skipped: comparing the noise floor of two vocoders is comparing
/// their dither.
SpectralAgreement compareSpectra(const juce::AudioBuffer<float>& native,
                                 const juce::AudioBuffer<float>& incumbent, int shiftSamples,
                                 double floorDb = -60.0);

// =============================================================================
// Invariants, where there is no null to be had
// =============================================================================

/**
 * What can still be asserted of a pair that will never null.
 *
 * An external plugin is the case this exists for. It may dither, it may hold
 * internal state from however it was last called, and two hosts calling it are
 * not obliged to agree on a sample. Comparing residuals there produces a number
 * that is about the plugin rather than about either engine, and a tolerance wide
 * enough to pass it is wide enough to pass anything.
 *
 * So the questions change rather than the bar moving. Each of these is a
 * property of one render, checked on both, and a claim about the pair that does
 * not depend on them agreeing sample for sample.
 *
 * Event order and latency belong to this tier too and are not here: the MIDI
 * comparison below already compares streams in order, and latency is asserted
 * where it is answered, which is the plan rather than the render.
 */
struct InvariantOptions {
    double sampleRate = 44100.0;

    /// The largest step either render may take between one sample and the next.
    ///
    /// Required rather than defaulted, and the corpus refuses a case in this
    /// tier without one. A default would be a number nobody chose applied to
    /// material nobody looked at: impulse material steps by full scale in one
    /// sample legitimately, so a bound that suits a synth pad would fail it, and
    /// a bound loose enough for an impulse would pass the click this check
    /// exists to catch.
    double maxStepPerSample = 0.0;

    /// The tail is the last @p tailSeconds of the render, and it has to have
    /// decayed below @p tailFloorDb. A device left running past the end of its
    /// material is how a graph transition leaks, and a residual comparison would
    /// not see it if both engines leaked alike.
    double tailSeconds = 0.05;
    double tailFloorDb = -60.0;
};

struct InvariantComparison {
    bool finite = false;
    bool lengthsMatch = false;
    bool continuous = false;
    bool tailDecays = false;

    /// The worst step found, and where, per side. Printed whether or not the
    /// bound held: a step creeping towards the bound is worth seeing before it
    /// crosses it.
    double worstStepNative = 0.0;
    double worstStepIncumbent = 0.0;
    std::int64_t worstStepAt = -1;

    double tailPeakNativeDb = -std::numeric_limits<double>::infinity();
    double tailPeakIncumbentDb = -std::numeric_limits<double>::infinity();

    /// Set when the pair could not be checked at all, as distinct from failing
    /// a check. A refusal is never reported as a passed invariant.
    std::string refusal;

    std::vector<std::string> problems;

    bool passed() const {
        return refusal.empty() && finite && lengthsMatch && continuous && tailDecays;
    }
};

InvariantComparison compareInvariants(const juce::AudioBuffer<float>& native,
                                      const juce::AudioBuffer<float>& incumbent,
                                      const InvariantOptions& options);

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

    /// Whether those messages agree, message for message, in status, value and
    /// instant. Counting them was not enough: equal counts would pass whatever
    /// they carried, and an MPE note opens with a channel pressure.
    bool otherMessagesMatch = false;

    /// Every disagreement, addressed: which note, which controller, which
    /// instant. A parity failure that says "the streams differ" is a day of
    /// somebody's life.
    std::vector<std::string> problems;

    bool passed() const {
        return notesMatch && controllersMatch && otherMessagesMatch;
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

/**
 * @brief What can be true of two engines rendering the same audio.
 *
 * A determinism class rather than a per-case preference, which is the whole
 * point of naming them: a case does not choose how strictly it is judged, it
 * declares what stands between the two engines, and the tier follows from that.
 * A tier is never a way to make a failing case pass. The rule the corpus was
 * calibrated under still holds: change the shape of the comparison, never the
 * size of the allowance.
 *
 * `Scripted` from the issue's list is deliberately absent. A launcher, a
 * monitoring or a recording case is not an offline render of a range, so it
 * needs a different runner rather than a different verdict, and a value here
 * would be a promise this one does not keep.
 */
enum class AudioTier {
    /// No audio assertion at all. The MIDI comparison carries the case.
    None,

    /// Sample for sample, with nothing allowed for. Deterministic internal DSP
    /// and simple routing, where nothing interpolates between the two.
    Exact,

    /// Sample for sample after one pinned offset, measured at the start and
    /// asserted against what the engine predicts. Latency, priming, and
    /// anything else whose whole effect is a delay.
    Aligned,

    /// The pinned shift, envelope timing, and a magnitude bound. What is left to
    /// ask when a phase vocoder stands between the two and the waveforms can
    /// never converge.
    Spectral,

    /// Finite, equal length, bounded discontinuity, decayed tail. What is left
    /// to ask of a nondeterministic external plugin, which owes nobody a sample.
    Invariants,

    /// Measured and printed, asserted only to be finite. One case, which exists
    /// to say how far apart the two stretchers are on broadband material.
    Measured,
};

const char* toString(AudioTier tier);

/// What a case rendered under, printed wherever it differs from the corpus
/// default. A residual that is really a fixture difference is the failure this
/// exists to prevent: without the environment beside the number, a case at
/// another rate looks like an engine that went wrong at one.
struct CaseEnvironment {
    double sampleRate = 44100.0;
    int blockSize = 512;
    int channels = 2;
    std::uint32_t seed = 0;

    bool operator==(const CaseEnvironment&) const = default;
};

struct CaseReport {
    std::string name;
    AudioTier tier = AudioTier::Exact;
    CaseEnvironment environment;

    bool hasAudio = false;
    AudioComparison audio;

    bool hasMidi = false;
    MidiComparison midi;

    /// What the two legs are offset by, measured even where the case does not
    /// align by it. Printed on every run: an offset that is not applied is
    /// still the first thing worth knowing about a case that will not null.
    bool hasMeasuredShift = false;
    double measuredShift = 0.0;
    double shiftCorrelation = 0.0;
    double shiftCorrelationEnvelope = 0.0;

    /// What the engine primed its stretcher with, printed beside the measured
    /// offset so the two can be read against each other. That relationship is
    /// what a Shift case's prediction has to be written from.
    int primingSamples = 0;

    /// Set on a stretched case, where a waveform comparison is the wrong
    /// question and these two are the right ones.
    bool hasSpectral = false;
    EnvelopeAgreement envelope;
    SpectralAgreement spectra;

    /// Set on a case in the Invariants tier, where there is no null to be had
    /// and the questions change instead of the bar moving.
    bool hasInvariants = false;
    InvariantComparison invariants;

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
 *
 * @p standard is what most of the corpus renders under, printed once in the
 * header. A case that deviates prints its own environment on its own line, so
 * that a number measured somewhere else can never be read as a number measured
 * here.
 */
std::string formatReport(const std::vector<CaseReport>& cases, const CaseEnvironment& standard);

/// dBFS from a linear amplitude, with a printable answer for silence.
std::string formatDb(double db);

}  // namespace magda::nulldiff
