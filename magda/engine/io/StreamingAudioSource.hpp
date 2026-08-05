#pragma once

#include <cstdint>

#include "exec/EngineDevice.hpp"
#include "io/PrefetchStream.hpp"

/**
 * @file StreamingAudioSource.hpp
 * @brief One placed file, rendered through a prefetch stream.
 *
 * The seam the plan already has: a ClipAudio op names a track, the host binds
 * an EngineAudioSource to it, and this is what stands behind that binding for
 * audio on the arrangement.
 *
 * Deliberately one file and one placement. Which clip is playing at a given
 * moment, what a crossfade between two of them sounds like, and what stretch
 * and warp do to the mapping are the clip layer's questions (#1890); this is
 * the layer that gets samples off a disk in time, and it is complete when a
 * region of a file plays where the model says it should.
 */

namespace magda::engine {

/**
 * @brief Where a file sits on the timeline, and where playback starts in it.
 *
 * In seconds rather than beats. The clip layer positions clips in beats, as the
 * model does, and resolves them to seconds through the tempo map before they
 * reach here; a source that had to do it itself would need a tempo map on the
 * audio thread, and there would then be two of them.
 */
struct ClipPlacement {
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    /// The sample of the file that plays at @ref startSeconds. What trimming
    /// the front of a clip moves.
    std::int64_t sourceOffsetSamples = 0;
};

class StreamingAudioSource final : public EngineAudioSource {
  public:
    /// The stream is owned elsewhere, because the prefetch thread reads it and
    /// the source is what a plan swap can replace.
    StreamingAudioSource(PrefetchStream& stream, const ClipPlacement& placement);

    void prepare(const RenderContext& context) override;

    /**
     * @brief Render the part of this block the clip covers.
     *
     * The rest is silence: a source fills its whole block, and a clip that
     * starts a hundred samples in has a hundred samples of nothing in front of
     * it rather than a hundred samples of whatever the buffer held.
     *
     * A block the transport is not moving through renders nothing at all. A
     * stopped timeline is not a held sample, it is no material.
     *
     * Rate conversion is not done here. The stream delivers the file's own
     * samples, and a file recorded at another rate than the device runs at
     * plays at the wrong pitch until the clip layer resamples it; that layer
     * owns stretch and warp and this is the same question.
     */
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override;

    /**
     * @brief The file sample that plays at a moment on the timeline.
     *
     * Public because pointing the stream at a clip is done from outside it.
     * Whoever schedules playback knows where it is about to start before the
     * callback does, and a stream told in advance is one that does not have to
     * seek in the block that needs the samples.
     */
    std::int64_t sourceSampleAt(double seconds) const;

  private:
    PrefetchStream& stream_;
    ClipPlacement placement_;
    double sampleRate_ = 44100.0;
};

}  // namespace magda::engine
