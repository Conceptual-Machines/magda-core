#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

#include "NullDiffCompare.hpp"
#include "core/ClipInfo.hpp"
#include "core/TrackInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file NullDiffCase.hpp
 * @brief One project, as model values, plus what it claims about the two
 *        engines rendering it (#2040).
 *
 * Model values and nothing else. Both legs are handed the same case and build
 * their own world from it: the incumbent leg installs the clips in ClipManager
 * and syncs them into a te::Edit, the native leg compiles them into a plan and
 * a snapshot. Neither is allowed a private opinion about what the project is,
 * which is the whole reason a case is data rather than a pair of setup
 * functions.
 *
 * A case is built in code rather than loaded from a .mgd. A load produces the
 * model and both legs consume the model, so a file on disk would add a step
 * neither leg is testing and a binary to the repository. What it would buy is
 * covered by the project-load tests already.
 */

namespace magda::nulldiff {

/// A tempo change, as both legs need to see it. Step changes only, and that is
/// deliberate: the two engines resolve a ramped tempo differently (the engine
/// subdivides because there is no closed form across a curve, the fork
/// integrates its own way), so a render case with a ramp in it would report a
/// tempo disagreement as a clip bug. Ramps are pinned where the answer is a
/// number, in the tempo-map comparison.
struct TempoPoint {
    double beat = 0.0;
    double bpm = 120.0;
};

/// What a case wrote to disk and told the source pool about. Carried on the
/// case so that a leg never has to probe a file to find out what it is.
struct SourceFact {
    SourceId id = INVALID_SOURCE_ID;
    juce::String path;
    double sampleRate = 44100.0;
    double durationSeconds = 0.0;
};

struct Case {
    std::string name;

    /// What this case is for, in a phrase. Printed beside a failure, because
    /// "placement.trims failed" is only useful to whoever wrote it.
    std::string covers;

    /// What stands between the two engines on this case's paths, which decides
    /// what can be asserted of the audio. Not a per-case preference: a tier is
    /// declared because of what the signal goes through, and the mechanism
    /// below says what that is.
    AudioTier tier = AudioTier::Exact;

    /// Whether the captured MIDI streams are compared.
    ///
    /// Independent of the tier, because they are independent questions. A
    /// project with an instrument track and audio tracks asserts both, and the
    /// four MIDI-only cases assert this one with the audio tier at None.
    bool compareMidiStreams = false;

    /// Peak residual at or below this is a null. A case that raises it carries
    /// the mechanism below, and a raised floor with no mechanism does not
    /// belong in the corpus.
    double floorDb = -120.0;

    /// Why this case is not a plain null: what the divergence is and why it is
    /// pinned rather than tolerated. Empty for a Null verdict.
    std::string mechanism;

    // --- the project ---------------------------------------------------------

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::vector<ClipInfo> clips;
    std::vector<SourceFact> sources;
    std::vector<TempoPoint> tempo{{0.0, 120.0}};

    /// The groove templates the clips may name, as the fork keeps them: a
    /// <GROOVETEMPLATES> document. One string feeds both legs, which is what
    /// makes "the same groove" a fact rather than two parsers agreeing.
    juce::String grooveXml;

    // --- what to render ------------------------------------------------------

    double startBeat = 0.0;
    double endBeat = 16.0;

    // --- the environment -----------------------------------------------------
    //
    // Recorded on the case rather than assumed by the runner, and printed
    // beside the case's numbers wherever it deviates from the corpus default.
    // A residual that is really a fixture difference is the worst failure a
    // parity corpus can have, because it does not look like a bug in the
    // harness: it looks like a bug in the engine.
    //
    // Plugin versions belong in this block and are not here. Nothing hosts a
    // plugin yet, so the field would have no value to record; it goes in with
    // #1893, which is also the first thing that will need it.

    double sampleRate = 44100.0;
    int blockSize = 512;
    int channels = 2;

    /// What generated this case's material. Zero for the cases whose material
    /// is deterministic by construction, which is all of them but the one that
    /// plays noise.
    std::uint32_t seed = 0;

    // --- what is allowed for -------------------------------------------------

    /// What a Shift verdict expects the offset to be, in samples.
    ///
    /// Required rather than optional. Zero means nobody has declared one, and a
    /// Shift case with no declaration is refused rather than measured: the
    /// whole point of pinning a shift is that a clip in the wrong place cannot
    /// be absorbed into the alignment, and an alignment checked against nothing
    /// absorbs anything. The engine reports what its own stretcher primes with
    /// (NativeRender::primingSamples), which is the figure to declare here.
    int expectedShiftSamples = 0;

    /// How far the measured shift may sit from the expected one, as a
    /// proportion. The prediction is the stretcher's own priming latency scaled
    /// by the ratio it runs at, which is a figure the library reports rather
    /// than one anybody measured, so the allowance is for the rounding between
    /// the two and not for a clip in the wrong place.
    double shiftTolerance = 0.15;

    /// A stretched case is judged on its envelope and its magnitude spectrum
    /// rather than its waveform, because two vocoders primed differently never
    /// converge on one waveform: priming sets the initial phase state and phase
    /// in a vocoder is memory. These are the two bounds that replace the null,
    /// and each is taken from the first run with its mechanism named on the
    /// case.
    double envelopeToleranceSamples = 1.0;
    double spectralPercentile95Db = 0.0;

    /// How far the search may look for that shift.
    int maxShiftSamples = 8192;

    /// Notes the incumbent is expected to be late by, in beats. Non-zero for
    /// exactly one thing: the fork drops midiOffset on an unlooped arranger
    /// clip, so every note of such a clip lands offset.
    double declaredMidiShiftBeats = 0.0;

    /// A fixed sub-sample offset between the two renders, applied before
    /// comparing, with its mechanism in `mechanism`.
    ///
    /// This is not a tolerance and it is not fitted. It is a constant the
    /// corpus measured, declared here, and then aligned by: if the offset were
    /// anything other than constant, aligning by one number would not bring the
    /// case to the floor, so the null after alignment is the evidence that the
    /// mechanism is what it is claimed to be. Measured, mechanized, then
    /// nulled.
    double declaredFractionalShiftSamples = 0.0;

    /// How much earlier the fork ends every note, in seconds.
    ///
    /// Its own constant: MidiNote::getPlaybackTime nudges every note-off back
    /// by 0.0001 s "to make sure the ordering is correct", which is how it
    /// keeps an off ahead of an on at the same instant. The engine orders
    /// events at compile time instead and keeps the length the note was drawn
    /// at. Written here as the number it is, so that the day the fork changes
    /// it the corpus says so rather than absorbing it.
    double incumbentNoteEndEarlySeconds = 0.0001;

    /// The largest step this case's material may take from one sample to the
    /// next, for a case in the Invariants tier. Required there and refused
    /// without, because the tier has no residual to fall back on: a bound
    /// nobody declared would certify a check that never ran.
    double maxStepPerSample = 0.0;

    double startBpm() const {
        return tempo.empty() ? 120.0 : tempo.front().bpm;
    }

    bool capturesMidi() const {
        return compareMidiStreams;
    }

    CaseEnvironment environment() const {
        return {sampleRate, blockSize, channels, seed};
    }
};

/**
 * @brief Every case, with its material written into @p scratchDirectory.
 *
 * Material is generated here rather than checked in, and the choice of it per
 * case is what makes a residual mean something: impulses and steps where the
 * engines must agree sample for sample, a band-limited tone wherever an
 * interpolator or a stretcher stands between them. See NullDiffMaterial.hpp.
 *
 * Seeds the source pool as it goes, because both legs read a file's rate and
 * duration from there and a case that let them probe separately would have two
 * answers to one question.
 */
std::vector<Case> buildCorpus(const juce::File& scratchDirectory);

/**
 * @brief The corpus, built once per process.
 *
 * Every call to buildCorpus writes every case's material again, which is forty
 * odd files of twelve seconds each. That was affordable when it happened a
 * handful of times; it is what a test binary should do once. Eight call sites
 * across two files meant the same bytes were written eight times, and on a
 * machine whose filesystem is slower than this one that is minutes rather than
 * seconds.
 *
 * Keyed on the directory, so a binary that asks for two different scratch
 * directories gets two different corpora rather than the first one twice.
 */
const std::vector<Case>& sharedCorpus(const juce::File& scratchDirectory);

/// The groove template every grooving case names, and the one the runner
/// installs in both engines.
extern const char* const kGrooveName;

}  // namespace magda::nulldiff
