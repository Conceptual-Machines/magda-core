#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <optional>
#include <vector>

#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "clip/ClipVoice.hpp"
#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file ClipAudioSource.hpp
 * @brief What a track's audio clips sum to, block by block.
 *
 * The source behind a ClipAudio op. The plan names a track and nothing else
 * (RenderPlan.hpp); what that track plays is read here, from the clip snapshot,
 * which is why moving a clip never recompiles a plan.
 *
 * It decides nothing about overlaps. The snapshot has already resolved which
 * clips are audible where and what fades they play (#2003), so entries that
 * overlap simply sum: the lane's rules were applied when the snapshot was
 * compiled, and a crossfade reaching here is two voices each playing the fade
 * it was handed.
 *
 * ## The two sections
 *
 * The same class plays a track's arrangement and its session, and which one is
 * decided at construction (#2301). Everything below the choice is shared: the
 * spans, the fades, the stretching, the warping, the readers and the voices.
 * Writing a second source for the session would have been a second answer to
 * every one of those questions, and a clip dragged out of a slot onto the
 * timeline has to sound the same in both places.
 *
 * What the section changes is where the material sits, which for a slot is
 * nowhere. A slot is compiled at the origin (ClipSnapshot.hpp) and a launch
 * handle says where its run began, so a session block is this block with its
 * beat axis shifted by that origin: same samples, same seconds of buffer, a
 * different stretch of material under them. A split block -- one a launch or a
 * stop lands inside -- is two such shifts over two sub-blocks of the output,
 * which is how a clip starts on its beat rather than on a callback boundary.
 *
 * How many clips a track may sound at once is decided rather than discovered:
 * kMaxVoicesPerTrack, below. That is not how many readers a track may have,
 * which is larger and is ClipVoicePool.hpp's (kMaxReadersPerTrack), because a
 * reader has to exist before its clip is due while a voice is only needed while
 * it plays. So a block can be handed more entries than it can render, and a
 * track wanting more than either says so through @ref
 * ClipAudioSource::starvedVoices, because silence nobody counted is
 * indistinguishable from a gap in the material.
 */

namespace magda::engine {

/**
 * @brief Clips one track may sound in a single callback.
 *
 * Capacity, and only capacity. How many readers a track may have standing by is
 * kMaxReadersPerTrack, which is larger and answers a different question: this
 * one is what a callback can render at once, that one is how far ahead of the
 * transport the material has to be open. Collapsing the two starves clips that
 * fit here perfectly well and simply have no reader yet.
 *
 * Live, not overlapping. Everything a callback touches is live for that
 * callback, whether or not the clips overlap by a sample: they are rendered in
 * the same call, so each holds its own voice for it. At the default block that
 * makes back to back clips shorter than about a millisecond and a half count
 * against this, and a host running a large block stretches that to tens of
 * milliseconds. The pool measures against the same block for that reason, so
 * what it reports and what the callback enforces are the same thing.
 *
 * Sixteen. A crossfade is two, a clip dropped inside another is two, and a clip
 * made of several events is one voice per event (#1901), so the shapes a lane
 * can legitimately be in add up well past the obvious pair, and a lane of
 * slices adds up faster still.
 *
 * Past it the track reports rather than quietly dropping the extras, through
 * @ref ClipAudioSource::starvedVoices and ClipVoicePool::overSubscribed.
 */
constexpr int kMaxVoicesPerTrack = 16;

class ClipAudioSource final : public EngineAudioSource {
  public:
    /**
     * @brief The arrangement's source for @p trackId.
     *
     * Both feeds outlive it and are shared with every other track: what a track
     * plays and which readers are standing by are properties of the session,
     * not of any one source or any one plan.
     */
    ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams);

    /**
     * @brief The session's source for @p trackId.
     *
     * The same reading of the same material, positioned by @p handles instead
     * of by the timeline. The handle feed is the section selector: a source
     * given one plays the track's slots and a source without one plays its
     * arrangement, because being positioned by a launch is the whole of what
     * makes a slot a slot.
     *
     * @p handles outlives it and is shared with every other track, like the
     * other two.
     */
    ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams,
                    LaunchHandleFeed& handles);

    void prepare(const RenderContext& context) override;

    /**
     * @brief Sum this track's clips into @p out.
     *
     * On the audio thread. The buffer arrives uncleared and leaves filled
     * whatever happens: a track with no snapshot, no streams or no clips
     * renders silence rather than whatever the buffer held.
     */
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override;

    /**
     * @brief Clips that should have sounded and had no reader to sound through.
     *
     * Counted per clip per block, the way PrefetchStream counts an underrun and
     * for the same reason: a track asking for more simultaneous clips than it
     * was given streams for is silence that has to be visible. A rising count
     * means either the ceiling above is too low for the material or the
     * provisioning thread is not keeping up with the transport.
     */
    int starvedVoices() const {
        return starved_.load(std::memory_order_relaxed);
    }

  private:
    /// One entry that sounds in this block, and the reader it sounds through.
    struct Sounding {
        const AudioClipPlayback* clip = nullptr;
        const AudioEventPlayback* event = nullptr;
        PrefetchStream* stream = nullptr;

        /// What plays it at a speed that is not its file's, and what that one
        /// was cued with. Null and zero for a clip at its file's own speed.
        ClipStretcher* stretcher = nullptr;
        int preRoll = 0;

        /// The stretch of material this entry is played over, and where in the
        /// output it lands. The block itself for the arrangement; for a session
        /// slot, the same block with its beat axis moved onto the slot's own
        /// origin, narrowed to whichever sub-range of the block was playing.
        BlockInfo block;
        int outOffset = 0;
    };

    /// Where the streams for this track are, as one block's worth of lookups.
    struct Streams {
        const ClipStreamTable::Entry* first = nullptr;
        const ClipStreamTable::Entry* last = nullptr;
    };

    /// Entries of @p clips that reach into @p block, appended to @p sounding.
    /// Shared by both sections: what differs is the block handed in.
    void gather(const std::vector<AudioClipPlayback>& clips, const BlockInfo& block, int outOffset,
                const Streams& streams, std::array<Sounding, kMaxVoicesPerTrack>& sounding,
                int& soundingCount);

    /// The same, over whichever of this track's slots a handle has playing.
    void gatherSession(const TrackClipPlayback& track, const BlockInfo& block,
                       const Streams& streams, std::array<Sounding, kMaxVoicesPerTrack>& sounding,
                       int& soundingCount);

    /// The voice already playing this entry, or a free one, or null when every
    /// voice is busy.
    ClipVoice* voiceFor(ClipId clipId, EventId eventId);

    TrackId trackId_;
    ClipSnapshotFeed& clips_;
    ClipStreamFeed& streams_;

    /// The section, and where a slot's position comes from. Null is the
    /// arrangement, which needs no handles because the timeline is its handle.
    LaunchHandleFeed* handles_ = nullptr;

    std::array<ClipVoice, kMaxVoicesPerTrack> voices_;

    /// Working space one voice at a time renders into before being summed in.
    /// One is enough: voices are rendered one after another. Sized for the
    /// largest block the plan was prepared for, plus the most reading a block
    /// that long can consume (stretchScratchSamples).
    juce::AudioBuffer<float> scratch_;

    std::atomic<int> starved_{0};
};

}  // namespace magda::engine
