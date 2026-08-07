#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <memory>

#include "io/PrefetchStream.hpp"

/**
 * @file ClipStretcher.hpp
 * @brief Playing a clip at a speed that is not its file's.
 *
 * Everything below this reads a file: which of its samples answer a position,
 * mirrored, tiled and converted to the device's rate (io/SourceReaders.hpp).
 * Everything above it consumes one sample of that reading per output sample.
 * This is what sits between when those two numbers are not the same.
 *
 * It is not in the reading chain, and that is a decision rather than an
 * accident. That chain is random access and pure: two reads that overlap agree
 * to the last bit, which is what lets a bounce resolve the same samples the
 * callback did. A stretcher is sequential state with a warm-up, so putting one
 * down there would quietly cost that property. It is not in the voice either,
 * because a voice is claimed and released per block and a stretcher that was
 * rebuilt whenever a voice changed hands would allocate on the audio thread.
 *
 * So it lives beside the stream, one per provisioned event, made and configured
 * by ClipVoicePool on the thread that is allowed to wait, and carried to the
 * callback in the same table (ClipStreamFeed.hpp). Three things follow, and they
 * are the answers to what a stretcher does about the transport moving:
 *
 * - a plan swap does nothing to it, because it never enters a plan epoch;
 * - a loop wrap does nothing to it, because tiling happens below the stream and
 *   a wrap is a discontinuity in the material rather than a change of position;
 * - a locate resets and re-primes something that already exists, which costs no
 *   allocation and no file handle.
 *
 * Latency is answered here rather than reported upwards. Every implementation
 * asks for @ref preRollSamples of material from *before* the first sample that
 * is to be heard, and the pool cues the stream that far back, so a voice's first
 * read is one contiguous read that begins with the priming samples. What comes
 * out of @ref process is then aligned with what an unstretched voice on the same
 * track is playing, and a ClipAudio op goes on reporting no latency at all.
 */

namespace magda::engine {

/**
 * @brief What one event asks of a stretcher.
 *
 * Compared rather than remembered: the pool holds the setup a stretcher was made
 * for and rebuilds when an edit no longer matches it, the same way it rebuilds a
 * reader whose file or reading changed.
 */
struct StretchSetup {
    /// The pinned project-file integer, and what actually runs rather than the
    /// raw field: a mode left at Off still stretches when something else forced
    /// a stretcher on (AudioEvent::getEffectiveTimeStretchMode). kDisabled here
    /// with a rate that is not one is a clip that resamples instead, which is
    /// what analog pitch is.
    int mode = 0;

    float semitones = 0.0f;

    int numChannels = 2;
    double sampleRate = 44100.0;
    int maxBlockSamples = 512;

    /// Reading samples consumed per output sample, at the event's usual rate.
    /// What the pre-roll is sized against; the rate a block actually runs at is
    /// whatever that block's own two positions say, which is how a moving auto
    /// tempo ratio costs nothing.
    double nominalRate = 1.0;

    /// Whether either of the clip's edges ramps its speed rather than its gain.
    /// A clip at its file's own speed still needs something that can land
    /// between samples if it does, because for the length of the ramp it is not
    /// at its file's own speed at all.
    bool speedRamp = false;

    bool operator==(const StretchSetup&) const = default;
};

class ClipStretcher {
  public:
    virtual ~ClipStretcher() = default;

    /**
     * @brief How far ahead of the nominal position the reading is consumed.
     *
     * Zero for a stretcher, which is handed the samples an output block spans
     * and nothing more. Not zero for the resampling path, whose curve reaches
     * past the sample it lands on: a sequential stream cannot be read twice, so
     * the read runs a fixed few samples ahead and the curve looks backwards
     * into what it kept. A constant offset rather than a cursor, so a position
     * is still derived from the timeline and nothing drifts.
     */
    virtual int readAheadSamples() const {
        return 0;
    }

    /**
     * @brief Material before the first sample heard that priming needs.
     *
     * At @p rate, because what a stretcher holds back depends on how fast it is
     * being asked to run. The pool cues a stream this far behind where the event
     * starts, so the samples exist by the time a voice asks for them.
     */
    virtual int preRollSamples(double rate) const = 0;

    /// Forget everything. Follows the transport jumping, and is always followed
    /// by a @ref prime. Allocation-free: what a reset drops is state, not memory.
    virtual void reset() = 0;

    /**
     * @brief Take up the material leading to @p until and align to it.
     *
     * On the audio thread, in the block a voice starts or resumes in. @p rate is
     * the reading consumed per output sample at that moment.
     *
     * @p samples is how much material to take, and is passed rather than asked
     * for so that it is the same number the stream was cued with. Asking again
     * here would answer for the rate this block happens to run at, which under
     * a tempo curve is not the rate the cue was worked out at, and a first read
     * that begins a few samples from where the stream was pointed is a seek.
     *
     * Reads through @p stream, and leaves it pointed exactly at @p until, so the
     * block that follows continues the same read rather than seeking. That is
     * also why the pre-roll is read here rather than handed in: how much
     * material priming wants is the implementation's own business and runs to
     * tens of thousands of samples for a phase vocoder, so the buffer for it
     * belongs to the stretcher that needs it rather than to every track that
     * has one.
     *
     * Short is allowed, and is what a clip cued at the very start of its own
     * file gets, and what a locate gets while the reader is still catching up.
     * Alignment is then as good as the material available makes it, which is the
     * incumbent's answer too.
     */
    virtual void prime(PrefetchStream& stream, std::int64_t until, int samples, double rate) = 0;

    /**
     * @brief Turn @p input into exactly @p output.
     *
     * On the audio thread. The ratio is whatever the two lengths say, which is
     * what makes a tempo curve and a speed ramp free: a block that has to
     * consume more reading than the last one simply passes more in.
     *
     * @p offset is where output sample zero sits relative to input sample zero,
     * in input samples, and @p step is how far apart consecutive output samples
     * are in them. Both matter only to the resampling path, which lands between
     * samples; a stretcher is aligned by priming and reads the ratio off the
     * lengths.
     */
    virtual void process(juce::dsp::AudioBlock<const float> input, double offset, double step,
                         juce::dsp::AudioBlock<float> output) = 0;

  protected:
    /**
     * @brief The buffer @ref prime reads its material into.
     *
     * Allocated once, by the implementation, on the thread that made it. What it
     * holds is worst case for the rate the event usually runs at rather than for
     * the fastest rate anything may ever run at, because that is what the event
     * will actually ask for; a rate that has since climbed past it primes with
     * what fits, which is the same short-pre-roll case as a clip at the very
     * start of its file.
     */
    void allocatePreRoll(int numChannels, int numSamples);

    /// @p wanted samples ending at @p until, or as many of them as fit and as
    /// the stream had. On the audio thread.
    juce::dsp::AudioBlock<const float> readPreRoll(PrefetchStream& stream, std::int64_t until,
                                                   int wanted);

  private:
    juce::AudioBuffer<float> preRoll_;
};

/**
 * @brief A stretcher for @p setup, or null when the event asks for nothing.
 *
 * Off the audio thread: every implementation allocates its working buffers here
 * and none of them allocates again. Null is the ordinary answer for a clip
 * playing its file at its file's own speed, which then reads through no extra
 * layer at all and pays for none.
 */
std::unique_ptr<ClipStretcher> makeStretcher(const StretchSetup& setup);

/**
 * @brief The rate a stretcher will refuse to go beyond.
 *
 * The incumbent's limits, and they are the reason a scratch buffer can be sized
 * at all: a block consumes at most this much reading per output sample, so the
 * most a voice can be asked to read in one callback is bounded before the
 * callback happens.
 */
constexpr double kMinStretchRate = 0.1;
constexpr double kMaxStretchRate = 10.0;

/**
 * @brief The most reading one block may consume.
 *
 * Bounded only because the rate is (@ref kMaxStretchRate): without a ceiling on
 * how fast a clip may play there would be no size to allocate a buffer at, and
 * every buffer here has to be allocated before the callback.
 *
 * It is one function because more than one thing is sized against it: the
 * voice's scratch, the interleaving a pipe needs, and the bound the voice
 * clamps a block's reading to. Two of those disagreeing by a few samples is a
 * write past the end of the third, so they are not allowed to be separately
 * derived.
 *
 * The clamp is what makes the ceiling true rather than merely intended. A rate
 * can exceed it: a constant ratio is clamped where it is read, but auto tempo's
 * is the project's tempo over the file's own and a file analysed at an absurd
 * bpm can ask for anything. Bounding what is consumed rather than the position
 * itself keeps the position map pure, which is what makes one block's reading
 * end where the next one's begins; a clip past the ceiling then plays wrongly,
 * the way one with a ratio past the ceiling already does, rather than writing
 * where it should not.
 */
inline int maxReadingSamples(int maxBlockSamples) {
    return static_cast<int>(maxBlockSamples * kMaxStretchRate) + 1;
}

/**
 * @brief Working space one voice needs for one block.
 *
 * What it renders, and behind that the most reading a block of that length can
 * consume.
 */
inline int stretchScratchSamples(int maxBlockSamples) {
    return maxBlockSamples + maxReadingSamples(maxBlockSamples);
}

}  // namespace magda::engine
