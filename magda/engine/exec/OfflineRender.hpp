#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <functional>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderContext.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TransportClock.hpp"

/**
 * @file OfflineRender.hpp
 * @brief The same plan, pulled by something that is not an audio device.
 *
 * Bounce, freeze and the null-diff harness are one question: render this
 * stretch of the timeline as fast as the machine will do it. Nothing about the
 * plan changes for it. The executor, the ops and the transport are the ones
 * playback uses, and that is the whole point: a harness that rendered through a
 * second implementation would prove things about the second implementation.
 *
 * What differs is who is pulling. There is no callback to be late for, so a
 * source may wait for a disk (io/FileAudioSource), nothing counts an underrun,
 * and the block size is a batching choice rather than a property of a device.
 * Output must not depend on it, and there is a test that says so.
 */

namespace magda::engine {

/** What to render, and how much of the machine to hand it. */
struct OfflineRenderRequest {
    /// The stretch of timeline to render, in beats, half-open. Beats because
    /// that is what the model positions things in: a bounce of bars one to nine
    /// is a beat range, and what it is worth in seconds is the tempo map's
    /// answer rather than the caller's.
    double startBeat = 0.0;
    double endBeat = 0.0;

    /**
     * @brief How long to keep rendering after the range, for tails.
     *
     * In seconds, because a reverb tail is a duration and not a musical length:
     * it does not get shorter when the tempo goes up.
     *
     * Rendered with the transport stopped, which is already what the engine
     * means by a stopped block: the graph keeps running so tails ring out, and
     * the cursor stands still. Standing still is what makes it a tail rather
     * than more of the song, because a cursor that kept moving would start
     * whatever clip comes after the range and print it into the bounce.
     */
    double tailSeconds = 0.0;

    /// How much timeline to ask the executor for at a time. A pure batching
    /// choice: the render is sample-identical at any value, and larger is only
    /// faster. Held to what the plan was prepared for.
    int blockSize = 512;
};

/** What came of a render. */
struct OfflineRenderResult {
    /// Samples handed to the sink, tail included.
    std::int64_t samplesRendered = 0;

    /// Blocks the sink was called with.
    int blocksRendered = 0;

    /// True when the caller's shouldContinue said to stop. What was already
    /// written stays written: an abandoned bounce is a short file, not a
    /// corrupt one, and the caller is the one that knows which of those it
    /// wants to keep.
    bool cancelled = false;

    /**
     * @brief Nothing was rendered, and not because the range was empty.
     *
     * The executor was not prepared, or the values do not belong to the plan it
     * was prepared with. Refused rather than rendered, for the reason a publish
     * refuses the same thing: what the executor does with values it cannot
     * apply is render at unity, so the alternative is a bounce with every fader
     * at 0 dB and nothing anywhere saying why.
     */
    bool refused = false;
};

/**
 * @brief Where an offline render's audio goes.
 *
 * A block at a time rather than one buffer for the whole range, because the
 * range can be an hour long and a caller writing a file has no reason to hold
 * it. The buffer is the renderer's and is reused: a sink that wants to keep the
 * audio copies it.
 */
class OfflineRenderSink {
  public:
    virtual ~OfflineRenderSink() = default;

    /// Off the audio thread, so a sink may write a file, allocate and block.
    virtual void write(const juce::AudioBuffer<float>& block, int numSamples) = 0;
};

/**
 * @brief Render @p request through @p executor into @p sink.
 *
 * The executor has to be prepared already, for @p context, with the devices and
 * sources the render should use. Preparing it is the caller's because the
 * choices are the caller's: a bounce at 96 kHz of a session running at 44.1 is
 * a different context and therefore different device state, and a freeze of one
 * track is a different plan.
 *
 * @p tempo is the map the range is resolved through and the one the ops see. It
 * does not change during a render: an offline render is one pass over a
 * timeline whose tempo track is whatever it was when the render started.
 *
 * @p shouldContinue is asked once per block and may be empty. A render of a
 * long range is the one thing in the engine a user is left waiting for, so it
 * is interruptible from the start rather than once someone complains.
 *
 * The metronome is not rendered. It is not in the graph, it is never recorded,
 * and a click printed into a bounce is the oldest bug in audio software.
 */
OfflineRenderResult renderOffline(PlanExecutor& executor, const PlanValues& values,
                                  const RenderContext& context, const TempoMap& tempo,
                                  const OfflineRenderRequest& request, OfflineRenderSink& sink,
                                  const std::function<bool()>& shouldContinue = {});

}  // namespace magda::engine
