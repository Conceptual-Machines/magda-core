#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStretcher.hpp"
#include "clip/FadeCurves.hpp"
#include "exec/RenderContext.hpp"
#include "io/PrefetchStream.hpp"

/**
 * @file ClipVoice.hpp
 * @brief One entry of the snapshot, playing.
 *
 * A voice is one audio event of one clip, read through one prefetch stream.
 * It holds no decision about what should sound: the snapshot has already
 * resolved occlusion, crossfades and takes (#2003), so a voice plays a span
 * with fades and never looks at its neighbours. Two clips crossfading are
 * two voices each playing the fade it was handed, with nothing pairing them
 * up.
 *
 * The span crops what it reads, interior silences punch holes in what it
 * read, and fades shape the edges of the span rather than the clip's
 * placement -- the span is what the lane leaves audible, and what a
 * listener hears begin and end.
 *
 * A silence is read through, not around: it mutes material still running
 * underneath, so the stream reads straight across and the hole is cleared
 * afterward. Skipping the read would leave the reader unpointed, which is a
 * seek and costs a block of silence on the far side (#2016).
 *
 * Reverse, looping and rate conversion live underneath, in what the stream
 * reads through (io/SourceReaders.hpp): each is a question of which of a
 * file's samples answer a position, so each is a reader wrapped around a
 * reader, and a voice always consumes one sample of one forward stream per
 * output sample at the device's rate.
 *
 * Speed and pitch work the other way (#2037): they change how fast the
 * reading is consumed, not what it holds, so they sit above the stream. A
 * voice asks EventPlacement.hpp where in the reading the block's two ends
 * are, reads exactly that much, and hands it to the stretcher beside the
 * stream to come back as this block's length. A clip at its file's own
 * speed has no stretcher and reads one sample per sample -- the same code
 * path with nothing in the middle.
 */

namespace magda::engine {

class ClipVoice {
  public:
    void prepare(const RenderContext& context);

    /// The entry this voice is playing. Invalid ids when it is playing nothing.
    ClipId clipId() const {
        return clipId_;
    }
    EventId eventId() const {
        return eventId_;
    }
    bool playing(ClipId clip, EventId event) const {
        return clipId_ == clip && eventId_ == event;
    }

    /**
     * @brief Stop playing whatever it was playing.
     *
     * What makes the next entry to claim this voice start rather than continue,
     * which is what the launch ramp is applied on.
     */
    void release();

    /**
     * @brief Stop, carrying this voice's own last sample down into @p out.
     *
     * The stop edge belongs here for the reason the start edge does: a voice is
     * the only thing that knows what it alone was contributing, and one ramp
     * over a track's sum cannot isolate it while other voices keep sounding.
     *
     * @p offset is where in the block it stopped. The voice is not free until
     * the ramp is spent (@ref fading).
     */
    void releaseInto(juce::dsp::AudioBlock<float> out, int offset, int fadeSamples);

    /// Carry an unfinished release ramp into @p out. Called every block for a
    /// voice that is @ref fading, and does nothing for one that is not.
    void carryTail(juce::dsp::AudioBlock<float> out);

    /// Whether a release ramp is still sounding. Such a voice plays nothing and
    /// holds no entry, and may not be claimed until this goes false.
    bool fading() const {
        return stop_.active();
    }

    /**
     * @brief Add this block's contribution to @p out.
     *
     * On the audio thread. @p scratch is working space of at least
     * stretchScratchSamples(maxBlockSize), owned by the caller since one is
     * enough for every voice on a track (rendered one after another and
     * summed as they go). It holds what this voice renders and, behind it,
     * the reading that block was made from, which is longer than the block
     * whenever the clip plays faster than its file.
     *
     * @p stretcher is the one standing beside @p stream, or null for a clip
     * played at its file's own speed; @p preRoll is what that stretcher was
     * cued with (ClipStreamFeed.hpp).
     *
     * Returns false when nothing was added, the ordinary answer for a voice
     * whose entry does not reach into this block.
     */
    bool render(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                const BlockInfo& block, PrefetchStream& stream, ClipStretcher* stretcher,
                int preRoll, juce::dsp::AudioBlock<float> scratch,
                juce::dsp::AudioBlock<float> out);

  private:
    /**
     * @brief Render @p count samples of a clip that consumes its reading at a
     *        rate, feeding the stretcher on a fixed grid.
     *
     * A stretcher's output follows the sequence of sizes it's handed, and a
     * rate resolved from a block's own two ends would make the result a
     * function of how the callback was cut up -- which RenderContext.hpp
     * forbids, since block size is an I/O batching concept, not a precision
     * one (a bounce at 1024 disagreeing with playback at 128 is the audible
     * form of the same bug).
     *
     * So the timeline is divided into cells anchored to where the event
     * begins, the ratio is resolved per cell from the same
     * readingPositionAt, and the stretcher is fed one whole cell at a time --
     * what it receives is then a function of position on the timeline alone.
     * Cells rarely line up with blocks, so what a cell produces beyond the
     * block that asked for it is held here until the next one.
     *
     * @return whether every cell got the reading it asked for. The region is
     *         filled either way.
     */
    bool renderThroughCells(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                            const BlockInfo& block, PrefetchStream& stream,
                            ClipStretcher& stretcher, int preRoll,
                            juce::dsp::AudioBlock<float> scratch,
                            juce::dsp::AudioBlock<float> region, double windowStart, int count);

    /// Multiply the part of @p region inside [@p startSeconds, @p endSeconds)
    /// by a curve running across it, rising or falling.
    void applyFade(juce::dsp::AudioBlock<float> region, EdgeSample regionFirstSample,
                   const BlockInfo& block, double startSeconds, double endSeconds, FadeCurve curve,
                   bool rising) const;

    /// How much timeline one cell covers. Small enough that a curved rate is
    /// still nearly straight across one, and that is the only thing it has to
    /// be. It lives beside the stretcher because everything sized against one
    /// is sized against this (ClipStretcher.hpp).
    static constexpr int kCellSamples = kStretchCellSamples;

    double sampleRate_ = 44100.0;

    /// Where in the scratch the reading starts, and the most output anything is
    /// driven in at once: the longest block, or one cell, whichever is larger.
    /// Both live in the one buffer, and a device below the cell size still
    /// drives whole cells through (ClipStretcher.hpp).
    int maxBlockSamples_ = 512;

    ClipId clipId_ = INVALID_CLIP_ID;
    EventId eventId_ = INVALID_EVENT_ID;

    /// Whether this voice put anything out in the previous block. What tells a
    /// voice that is beginning from one that is carrying on, which is the
    /// difference the launch ramp is for.
    bool sounded_ = false;

    /// The launch ramp in progress, which outlives the block that started it.
    /// A ramp is 256 samples by default and a block can be shorter than that,
    /// so a ramp that lived inside one block would be a different length at
    /// every block size (FadeCurves.hpp).
    StartDeClick deClick_;

    /// What it was contributing when it ended, carried down without touching
    /// anything else on the track.
    StopDeClick stop_;

    /// One cell's output, and how much of it a block has taken. Allocated in
    /// prepare(), never on the callback.
    juce::AudioBuffer<float> held_;
    int pendingCount_ = 0;
    int pendingRead_ = 0;

    /// Whether the grid is running at all, and where its next cell begins, in
    /// samples of the timeline.
    bool pending_ = false;
    std::int64_t nextCell_ = 0;

    /// Samples of the first cell that lie before where playback resumed, and
    /// are produced only to be dropped. Never more than a cell.
    int skip_ = 0;

    /// The stretcher this voice primed, or null for one that has not primed
    /// anything. An identity rather than a flag: the pool replaces a
    /// stretcher without touching the stream whenever a rate, pitch or mode
    /// is edited, and the entry a voice is playing doesn't change when it
    /// does, so an unprimed fresh engine would otherwise reach the callback
    /// cold and stay cold -- a phase vocoder's whole latency, late for the
    /// rest of the take.
    ///
    /// Separate from @ref sounded_: a block that came back short is not a
    /// reason to prime again, since priming reads backwards and the reader
    /// is already behind -- doing it would seek and keep it behind for good.
    const ClipStretcher* primed_ = nullptr;
};

}  // namespace magda::engine
