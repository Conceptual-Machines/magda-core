#pragma once

#include <juce_core/juce_core.h>

namespace juce {
class KnownPluginList;
}

#include <limits>
#include <string>
#include <vector>

#include "NullDiffCompare.hpp"
#include "core/AutomationInfo.hpp"
#include "core/ClipInfo.hpp"
#include "core/TrackInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file NullDiffCase.hpp
 * @brief One project, as model values, plus what it claims about the native
 *        engine's render of it (#2040).
 *
 * Model values and nothing else. The native leg compiles the case into a plan
 * and a snapshot, so a case is data rather than a setup function.
 *
 * A case is built in code rather than loaded from a .mgd: a file on disk would
 * add a step the leg is not testing and a binary to the repository. The
 * project-load tests already cover what it would buy.
 */

namespace magda::nulldiff {

/// A tempo change. Step changes only: a ramped tempo has no closed form across
/// a curve, so a render case with a ramp would report a tempo error as a clip
/// bug. Ramps are pinned where the answer is a number, in the tempo-map
/// comparison.
struct TempoPoint {
    double beat = 0.0;
    double bpm = 120.0;
};

/**
 * @brief A slot this case launches, and the beat it is launched on (#2441).
 *
 * The corpus renders an offline arrangement and nothing launches by itself, so
 * a session clip put in a case stays silent until something says this. The leg
 * queues it before the render, against this beat, so the run starts on a known
 * sample.
 *
 * No quantization, and that is not an omission. Quantization is the host's:
 * MAGDA resolves a clip's LaunchQuantize into a beat and hands the engine that
 * beat (ClipSynchronizer::launchSessionClip), and the native side's requests
 * carry a beat for the same reason. What a case declares is therefore the beat
 * a launch was already resolved to, and where the grid put it is #2306's to
 * assert rather than this corpus's to null.
 */
struct LaunchInfo {
    TrackId trackId = INVALID_TRACK_ID;
    int sceneIndex = 0;

    /// The timeline beat to start the run on.
    ///
    /// The engine floors the offset it derives through the tempo map. A case
    /// that wants an exact expectation puts the launch on the render's own
    /// start beat, where the answer is sample zero and every block after it is
    /// the ordinary clip render #2306 says it is.
    double beat = 0.0;
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

    /// What the signal goes through on this case's paths, which decides what
    /// can be asserted of the audio. Not a per-case preference: the mechanism
    /// below says what the path is.
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

    /// The compiler diagnostics this case's project is expected to produce, as
    /// substrings of them.
    ///
    /// A diagnostic normally makes a case unmeasurable, and that is the right
    /// default: it says the plan could not carry something the model asked for,
    /// so whatever was rendered is not the project. A few cases are about a
    /// model the compiler is right to refuse, and there the diagnostic is the
    /// parity rather than an obstacle to it -- a rack chain routed to an aux
    /// output reaches nothing in either engine, and the plan says so out loud
    /// instead of dropping it silently.
    ///
    /// Declared as an expectation rather than a suppression, so it is asserted
    /// in both directions: a case that stops producing the diagnostic it named
    /// fails as loudly as one that produces a diagnostic it did not. The
    /// alternative -- a flag that ignores diagnostics -- would let a real
    /// refusal appear on such a case and be read as the expected one.
    std::vector<std::string> expectedDiagnostics;

    // --- the project ---------------------------------------------------------

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::vector<ClipInfo> clips;
    std::vector<SourceFact> sources;
    std::vector<TempoPoint> tempo{{0.0, 120.0}};

    /// The slots this case launches, in the order it wants them asked for.
    /// Empty for every case that renders an arrangement, which is all of them
    /// but the session ones.
    std::vector<LaunchInfo> launches;

    /// The automation the project plays, and the clips a clip-based lane names
    /// (#2123). Model values like the rest of a case; the engine compiles them
    /// into segment lanes.
    std::vector<AutomationLaneInfo> lanes;
    std::vector<AutomationClipInfo> automationClips;

    /// The groove templates the clips may name: a <GROOVETEMPLATES> document.
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

    /// Beats the reference notes are expected to land late by, declared by the
    /// case. Non-zero only for a clip whose midiOffset the reference drops.
    double declaredMidiShiftBeats = 0.0;

    /// A fixed sub-sample offset between the render and its reference, applied
    /// before comparing, with its mechanism in `mechanism`.
    ///
    /// This is not a tolerance and it is not fitted. It is a constant the
    /// corpus measured, declared here, and then aligned by: if the offset were
    /// anything other than constant, aligning by one number would not bring the
    /// case to the floor, so the null after alignment is the evidence that the
    /// mechanism is what it is claimed to be. Measured, mechanized, then
    /// nulled.
    double declaredFractionalShiftSamples = 0.0;

    /// How much earlier the reference ends every note, in seconds. The engine
    /// orders events at compile time and keeps the length the note was drawn at.
    double referenceNoteEndEarlySeconds = 0.0001;

    /// Whether this case's audio changes when a note ENDS, not only when it
    /// starts. Set by hand: the harness cannot tell a synth that sustains from
    /// one that does not, and the nudge above is only audible to the first.
    bool audioChangesAtNoteEnds = false;

    /// This case's instrument's release stage, in seconds.
    ///
    /// The note-end offset above moves the release by four samples, so an
    /// instrument with a release ramp differs for that ramp's whole length. A
    /// case keeps it short: the longer it is, the less of the render is held to
    /// bit identity.
    double noteEndReleaseSeconds = 0.0;

    /// The largest step this case's material may take from one sample to the
    /// next, for a case in the Invariants tier. Required there and refused
    /// without, because the tier has no residual to fall back on: a bound
    /// nobody declared would certify a check that never ran.
    double maxStepPerSample = 0.0;

    /// The peak this case's render has to reach, for a case in the Invariants
    /// tier. Required there and refused without, for the reason the step bound
    /// is: this tier computes no residual, so nothing else in it would notice a
    /// silent render, and every other check it makes is satisfied by silence.
    ///
    /// A liveness floor rather than a level. It says the render happened, and
    /// it is set far below what the project renders and far above what a broken
    /// one does.
    double minPeakDb = std::numeric_limits<double>::infinity();

    /// Assertions this case's hosted plugins are known to raise, as substrings
    /// of them (#2175).
    ///
    /// The assertion watch is a process-wide hook, so on a case that loads
    /// somebody else's code it cannot say whose code raised what. Named here
    /// rather than waived by the presence of a plugin, because "this project
    /// hosts a VST3" is not evidence about who asserted: a waiver keyed on it
    /// forgives a genuine engine regression on exactly the three cases that
    /// were worth adding, and forgives it on a plugin that sits on a bypassed
    /// chain and was never instantiated.
    ///
    /// Asserted in both directions, like @ref expectedDiagnostics: an assertion
    /// nothing named still condemns the case, and a name that stops firing
    /// fails as loudly, so the day a plugin is fixed the line comes out rather
    /// than sitting there forgiving something that no longer happens.
    std::vector<std::string> expectedHostedAssertions;

    /// Whether this case's render window goes past the end of its material
    /// (#2175).
    ///
    /// True for every case built in code: they are written to render what they
    /// contain and then stop. False for a real project, whose arrangement is
    /// minutes long and whose case renders eight beats of it, so the render ends
    /// in the middle of a clip.
    ///
    /// Read by the invariants tier, where the tail check asks what is left
    /// running after the material stops. That question has no meaning on a
    /// render that stops first: what is in the last fifty milliseconds there is
    /// the music. The tail is still measured and still printed, marked so that a
    /// number nobody asked for cannot be read as a check that held.
    bool rendersPastItsMaterial = true;

    /// How far this project's renders at two different block sizes may sit
    /// apart, as a peak level (#2078).
    ///
    /// Negative infinity is bit identity written as a level, and it is the
    /// default because it is what RenderContext already requires: output is a
    /// function of timeline position and block size is an I/O batching concept.
    /// A project of internal devices differs nowhere at all, at 64, at 512 and
    /// at 4096, or the engine is wrong.
    ///
    /// A case raises it only for a project that hosts an external plugin, which
    /// owes nobody a sample and is entitled to frame its own work how it
    /// likes. The gate does not take the raised number on trust: it refuses one
    /// on a project with no external plugin in it, so the epsilon can only ever
    /// be bought by something there is to attribute the difference to. That is
    /// the difference between attributing it and absorbing it.
    double blockSizeEpsilonDb = -std::numeric_limits<double>::infinity();

    double startBpm() const {
        return tempo.empty() ? 120.0 : tempo.front().bpm;
    }

    bool capturesMidi() const {
        return compareMidiStreams;
    }

    /// Whether this case is held to bit identity across block sizes, which is
    /// the default and what every project of internal devices owes (#2078).
    ///
    /// Asked as "is it still the default" rather than as "is the bound finite",
    /// because those differ on exactly the value that matters. Positive infinity
    /// is not finite, so a finiteness test lets it through as if it were the
    /// strict default, while the gate reads it as a bound that admits every
    /// residual there is: the declaration rule and the comparison would then
    /// both be disabled by the same value, each because the other was assumed to
    /// have caught it.
    bool demandsBitIdenticalBlocks() const {
        return blockSizeEpsilonDb == -std::numeric_limits<double>::infinity();
    }

    CaseEnvironment environment() const {
        // Plugins left empty on purpose: a case declares what it renders
        // under and cannot declare what is installed. The runner fills them in
        // from what the render actually resolved.
        return {sampleRate, blockSize, channels, seed, {}};
    }
};

/**
 * @brief Every case, with its material written into @p scratchDirectory.
 *
 * Material is generated here rather than checked in, and the choice of it per
 * case is what makes a residual mean something: impulses and steps where the
 * render must be sample exact, a band-limited tone wherever an interpolator or
 * a stretcher is in the path. See NullDiffMaterial.hpp.
 *
 * Seeds the source pool as it goes, so a file's rate and duration have one
 * answer instead of being probed separately.
 */
std::vector<Case> buildCorpus(const juce::File& scratchDirectory);

/// The focused #2457 reproducer: a Session slot begins one second into a
/// steady source, so its launch edge is a full-scale step unless corrected.
Case buildTrimmedSessionLaunchCase(const juce::File& scratchDirectory);

/// The corresponding source-boundary attack, which #2444 requires launch
/// correction to preserve exactly.
Case buildUntrimmedSessionAttackCase(const juce::File& scratchDirectory);

/// A trimmed arrangement clip is ordinary timeline playback, not a Session
/// launch transition, and remains unshaped.
Case buildTrimmedArrangementCase(const juce::File& scratchDirectory);

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

/**
 * @brief The external plugins @p value hosts, named, in chain order (#2078).
 *
 * Every device on every track and inside every rack, master included, whose
 * format is not PluginFormat::Internal. Named rather than counted, because what
 * this answers is "what could account for this difference", and a count answers
 * nothing.
 *
 * This is what buys a project the epsilon in the block-size gate. A project of
 * internal devices is held to bit identity at every block size and has nobody
 * to blame if it is not; a project hosting a plugin is compared within the
 * bound it declares, with these names printed beside the residual. The
 * distinction has to come from the project rather than from the case's tier: a
 * tier says what can be asserted of the audio, and an external plugin
 * frames its own work whether or not the case chose to notice.
 *
 * Empty for the code-built corpus, whose devices were all written for it. The
 * three real projects that host a plugin (#2175) are what fills it, and they
 * found the gate it was written for rather than widening it.
 */
std::vector<std::string> externalDevicesIn(const Case& value);

/**
 * @brief Of those, the ones @p knownPlugins has never seen (#2175).
 *
 * The gate on a case whose project hosts a plugin. A project rendered without
 * the plugin it names is a different project: the slot binds nothing and the
 * chain around it renders, a case that passes by having tested less than it
 * claims. So the question is asked of the scan before the leg is driven, and a
 * case with an answer does not run at all.
 *
 * Asked of the model rather than of the compiled plan, which makes it strict in
 * one direction: a plugin on a chain the project bypassed is never instantiated
 * and never missed, and this refuses the case over it anyway. That is the right
 * way round to be wrong -- the cost is a case reported as not run on a machine
 * that could have measured it, and the report names the plugin, where the other
 * way round is a green case that rendered a project nobody has.
 *
 * A null @p knownPlugins is not a special case: nothing is installed, so
 * everything the project names is absent. That is the state every CI runner is
 * in.
 */
std::vector<std::string> absentPluginsIn(const Case& value,
                                         const juce::KnownPluginList* knownPlugins);

/// The groove template every grooving case names, and the one the runner
/// installs.
extern const char* const kGrooveName;

}  // namespace magda::nulldiff
