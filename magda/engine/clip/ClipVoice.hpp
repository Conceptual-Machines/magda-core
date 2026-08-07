#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "clip/ClipSnapshot.hpp"
#include "exec/RenderContext.hpp"
#include "io/PrefetchStream.hpp"

/**
 * @file ClipVoice.hpp
 * @brief One entry of the snapshot, playing.
 *
 * A voice is one audio event of one clip, read through one prefetch stream. It
 * holds no decision about what should sound: the snapshot has already resolved
 * occlusion, crossfades and takes, so a voice plays a span with fades and never
 * looks at its neighbours (#2003). Two clips crossfading are two voices each
 * playing the fade it was handed, and nothing pairs them up.
 *
 * The span crops what it reads, the interior silences punch holes in what it
 * read, and the fades shape the edges of the span rather than the edges of the
 * clip's placement: the span is what the lane leaves audible, and that is what
 * a listener hears begin and end.
 *
 * Reading through a hole rather than around it is deliberate. A silence mutes
 * material that is still running underneath, so the stream is read straight
 * across and the hole is cleared afterwards; skipping the read would leave the
 * reader somewhere it was not pointed, which is a seek and costs a block of
 * silence on the far side (#2016).
 *
 * Not here: rate conversion, looping and reverse (#2036), stretch and pitch
 * (#2037), warp (#2038). A voice reads one file sample per output sample.
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
     * @brief Add this block's contribution to @p out.
     *
     * On the audio thread. @p scratch is working space of at least the block's
     * length, owned by the caller because one is enough for every voice on a
     * track: they are rendered one after another and summed as they go.
     *
     * Returns false when nothing was added, which is the ordinary answer for a
     * voice whose entry does not reach into this block.
     */
    bool render(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                const BlockInfo& block, PrefetchStream& stream,
                juce::dsp::AudioBlock<float> scratch, juce::dsp::AudioBlock<float> out);

  private:
    /// Multiply the part of @p region inside [@p startSeconds, @p endSeconds)
    /// by a curve running across it, rising or falling.
    void applyFade(juce::dsp::AudioBlock<float> region, int regionFirstSample,
                   const BlockInfo& block, double startSeconds, double endSeconds, FadeCurve curve,
                   bool rising) const;

    double sampleRate_ = 44100.0;

    ClipId clipId_ = INVALID_CLIP_ID;
    EventId eventId_ = INVALID_EVENT_ID;

    /// Whether this voice put anything out in the previous block. What tells a
    /// voice that is beginning from one that is carrying on, which is the
    /// difference the launch ramp is for.
    bool sounded_ = false;
};

}  // namespace magda::engine
