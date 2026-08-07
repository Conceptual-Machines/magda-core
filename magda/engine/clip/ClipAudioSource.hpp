#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "clip/ClipVoice.hpp"
#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"

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
 * How many clips a track may sound at once is decided rather than discovered.
 * kMaxVoicesPerTrack streams are provisioned per track (ClipVoicePool.hpp), and
 * a track wanting more says so through @ref starvedVoices, because silence
 * nobody counted is indistinguishable from a gap in the material.
 */

namespace magda::engine {

/**
 * @brief Clips one track may sound at the same instant.
 *
 * Eight rather than two. A crossfade is two, a clip dropped inside another is
 * two, and a clip made of several events is one voice per event (#1901), so the
 * shapes a lane can legitimately be in add up past the obvious pair. What eight
 * is not is a guess at how many a user might stack: past it the track reports
 * rather than quietly dropping the extras.
 *
 * Simultaneity, not throughput. A voice costs a few bytes of state, so this is
 * not the number that bounds memory; how many readers a track may have standing
 * by is kMaxReadersPerTrack, and clips that follow one another consume that
 * without ever needing two voices at once.
 *
 * At this resolution "sounding in the same block" and "sounding at the same
 * instant" are the same question. Two entries that share a block each hold a
 * voice for it whether or not their samples overlap, which stops mattering
 * somewhere below the ten clips a millisecond it would take to notice.
 */
constexpr int kMaxVoicesPerTrack = 8;

class ClipAudioSource final : public EngineAudioSource {
  public:
    /**
     * @brief The clip source for @p trackId.
     *
     * Both feeds outlive it and are shared with every other track: what a track
     * plays and which readers are standing by are properties of the session,
     * not of any one source or any one plan.
     */
    ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams);

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
    };

    /// The voice already playing this entry, or a free one, or null when every
    /// voice is busy.
    ClipVoice* voiceFor(ClipId clipId, EventId eventId);

    TrackId trackId_;
    ClipSnapshotFeed& clips_;
    ClipStreamFeed& streams_;

    std::array<ClipVoice, kMaxVoicesPerTrack> voices_;

    /// Working space one voice at a time renders into before being summed in.
    /// One is enough: voices are rendered one after another, and sized for the
    /// largest block the plan was prepared for.
    juce::AudioBuffer<float> scratch_;

    std::atomic<int> starved_{0};
};

}  // namespace magda::engine
