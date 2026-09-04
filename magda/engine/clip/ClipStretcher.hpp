#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "io/PrefetchStream.hpp"

/**
 * @file ClipStretcher.hpp
 * @brief Playing a clip at a speed that is not its file's.
 *
 * Everything below this reads a file: which of its samples answer a
 * position, mirrored, tiled and converted to the device's rate
 * (io/SourceReaders.hpp). Everything above it consumes one sample of that
 * reading per output sample. This sits between when those two numbers
 * differ.
 *
 * Not in the reading chain: that chain is random access and pure (two
 * overlapping reads agree to the last bit, which is what lets a bounce
 * match the callback), while a stretcher is sequential state with a
 * warm-up. Not in the voice either, since a voice is claimed and released
 * per block and rebuilding a stretcher on every handoff would allocate on
 * the audio thread.
 *
 * So it lives beside the stream, one per provisioned event, made and
 * configured by ClipVoicePool on the thread that's allowed to wait, and
 * carried to the callback in the same table (ClipStreamFeed.hpp). That
 * placement answers what a stretcher does about the transport moving: a
 * plan swap doesn't touch it (it never enters a plan epoch); a loop wrap
 * doesn't either (tiling happens below the stream, so a wrap is a
 * discontinuity in the material, not a change of position); a locate
 * resets and re-primes something that already exists, with no allocation
 * and no file handle.
 *
 * Latency is answered here rather than reported upwards: every
 * implementation asks for @ref preRollSamples of material from before the
 * first sample heard, the pool cues the stream that far back, and a
 * voice's first read is one contiguous read starting with the priming
 * samples -- so @ref process comes out aligned with an unstretched voice
 * on the same track, and a ClipAudio op keeps reporting no latency at all.
 */

namespace magda::engine {

/**
 * @brief What one event asks of a stretcher.
 *
 * Compared rather than remembered: the pool holds the setup a stretcher was
 * made for and rebuilds it when an edit no longer matches, the same way it
 * rebuilds a reader whose file or reading changed.
 */
struct StretchSetup {
    /// The pinned project-file integer, and what actually runs rather than
    /// the raw field: a mode left at Off still stretches when something
    /// else forced a stretcher on (AudioEvent::getEffectiveTimeStretchMode).
    /// kDisabled with a non-unity rate is a clip that resamples instead,
    /// which is what analog pitch is.
    int mode = 0;

    float semitones = 0.0f;

    int numChannels = 2;
    double sampleRate = 44100.0;
    int maxBlockSamples = 512;

    /// Reading samples consumed per output sample, at the event's usual
    /// rate. What the pre-roll is sized against; a block actually runs at
    /// whatever its own two positions say, which is how a moving auto tempo
    /// ratio costs nothing.
    double nominalRate = 1.0;

    /// Whether either of the clip's edges ramps its speed rather than its
    /// gain. A clip at its file's own speed still needs something that can
    /// land between samples during the ramp, since it isn't at that speed
    /// for its length.
    bool speedRamp = false;

    /// Whether the clip follows the project's tempo. Carried because @ref
    /// nominalRate is only an average for one that does, and a clip
    /// averaging unity is still stretching in both directions around it --
    /// it needs an engine a clip truly pinned at unity does not.
    bool followsTempo = false;

    bool operator==(const StretchSetup&) const = default;
};

class ClipStretcher {
  public:
    virtual ~ClipStretcher() = default;

    /**
     * @brief How far ahead of the nominal position the reading is consumed.
     *
     * Zero for a stretcher, which is handed exactly the samples an output
     * block spans. Non-zero for the resampling path, whose curve reaches
     * past the sample it lands on: a sequential stream can't be read twice,
     * so the read runs a fixed few samples ahead and the curve looks back
     * into what it kept. A constant offset rather than a cursor, so
     * position is still derived from the timeline and nothing drifts.
     */
    virtual int readAheadSamples() const {
        return 0;
    }

    /**
     * @brief Material before the first sample heard that priming needs.
     *
     * At @p rate, since what a stretcher holds back depends on how fast
     * it's being asked to run. The pool cues a stream this far behind where
     * the event starts, so the samples exist by the time a voice asks.
     */
    virtual int preRollSamples(double rate) const = 0;

    /// Forget everything. Follows the transport jumping, and is always
    /// followed by a @ref prime. Allocation-free: a reset drops state, not
    /// memory.
    virtual void reset() = 0;

    /**
     * @brief Take up the material leading to @p until and align to it.
     *
     * On the audio thread, in the block a voice starts or resumes in. @p
     * rate is the reading consumed per output sample at that moment.
     *
     * @p samples is passed rather than recomputed here so it's the same
     * number the stream was cued with -- asking again would answer for
     * this block's rate, which under a tempo curve can differ from the
     * rate the cue used, making the first read a seek instead of a
     * continuation.
     *
     * Reads through @p stream and leaves it pointed exactly at @p until,
     * so the next block continues the read rather than seeking. The
     * pre-roll buffer is also owned by the implementation rather than
     * passed in, since how much material priming wants runs to tens of
     * thousands of samples for a phase vocoder and is the stretcher's own
     * business, not every track's.
     *
     * Short is allowed: a clip cued at the very start of its file gets
     * this, as does a locate while the reader is still catching up.
     * Alignment is then as good as the available material allows, matching
     * the incumbent.
     */
    virtual void prime(PrefetchStream& stream, std::int64_t until, int samples, double rate) = 0;

    /**
     * @brief Turn @p input into exactly @p output.
     *
     * On the audio thread. The ratio is whatever the two lengths say, which
     * makes a tempo curve and a speed ramp free: a block consuming more
     * reading than the last simply passes more in.
     *
     * @p offset is where output sample zero sits relative to input sample
     * zero, in input samples; @p step is the spacing between consecutive
     * output samples in them. Both matter only to the resampling path,
     * which lands between samples -- a stretcher is aligned by priming and
     * reads the ratio off the lengths.
     */
    virtual void process(juce::dsp::AudioBlock<const float> input, double offset, double step,
                         juce::dsp::AudioBlock<float> output) = 0;

  protected:
    /**
     * @brief The buffer @ref prime reads its material into.
     *
     * Allocated once, by the implementation, on the thread that made it.
     * Sized for the rate the event usually runs at rather than the fastest
     * rate it might ever hit, since that's what it will actually ask for; a
     * rate that's since climbed past it primes with what fits, the same
     * short-pre-roll case as a clip at the start of its file.
     */
    void allocatePreRoll(int numChannels, int numSamples);

    /// @p wanted samples ending at @p until, or as many as fit and the
    /// stream had. On the audio thread.
    juce::dsp::AudioBlock<const float> readPreRoll(PrefetchStream& stream, std::int64_t until,
                                                   int wanted);

  private:
    juce::AudioBuffer<float> preRoll_;
};

/**
 * @brief A stretcher for @p setup, or null when the event asks for nothing.
 *
 * Off the audio thread: every implementation allocates its working buffers
 * here and none again. Null is the ordinary answer for a clip playing its
 * file at its own speed, which then reads through no extra layer and pays
 * for none.
 */
std::unique_ptr<ClipStretcher> makeStretcher(const StretchSetup& setup);

/**
 * @brief The rate a stretcher will refuse to go beyond.
 *
 * The incumbent's limits, and the reason a scratch buffer can be sized at
 * all: a block consumes at most this much reading per output sample, so the
 * most a voice can be asked to read in one callback is bounded up front.
 */
constexpr double kMinStretchRate = 0.1;
constexpr double kMaxStretchRate = 10.0;

/**
 * @brief How much output a stretcher is driven in at a time.
 *
 * A voice feeds a stretcher on a fixed grid rather than a block at a time,
 * so what it receives follows position on the timeline rather than how the
 * callback was cut up (ClipVoice::renderThroughCells). Lives here rather
 * than with the voice because everything sized against a stretcher has to
 * be sized against it: a device running 64-sample blocks still drives whole
 * 128-sample cells through, and a capacity derived from the block would be
 * half of what one cell needs.
 */
constexpr int kStretchCellSamples = 128;

/**
 * @brief The most output a stretcher can be asked for in one go.
 *
 * The block, or one cell, whichever is larger. Below the cell size the
 * block stops being the unit anything is driven in, and sizing from it
 * would leave a stretcher writing half a cell and zero-filling the rest --
 * audible as alternating material and silence rather than a visible error.
 */
inline int stretchWorkSamples(int maxBlockSamples) {
    return std::max(maxBlockSamples, kStretchCellSamples);
}

inline int maxReadingSamples(int maxBlockSamples) {
    return static_cast<int>(stretchWorkSamples(maxBlockSamples) * kMaxStretchRate) + 1;
}

/**
 * @brief Working space one voice needs for one block.
 *
 * What it renders, plus the most reading a block of that length can consume.
 */
inline int stretchScratchSamples(int maxBlockSamples) {
    return stretchWorkSamples(maxBlockSamples) + maxReadingSamples(maxBlockSamples);
}

/**
 * @brief The largest run of input a stretcher is ever handed in one push.
 *
 * A cell's worth of reading, deliberately independent of block size
 * (#2078): a voice drives every stretcher one cell at a time
 * (ClipVoice::renderThroughCells), so a cell's reading is the true answer.
 *
 * This matters because a library given the same total split across a
 * different number of pushes is not guaranteed to end in the same state.
 * SoundTouch isn't: TDStretch::skipFract carries the leftover fraction of a
 * sample from one splice into the next, and neither clear() nor setTempo()
 * resets it. A warm-up sized from the block would make playback inherit a
 * different fraction at every block size -- output as a function of how the
 * callback happened to be cut up. It cost a decibel on the corpus at 4096
 * and was invisible at 512.
 */
inline int stretchPushSamples() {
    return maxReadingSamples(kStretchCellSamples);
}

}  // namespace magda::engine
