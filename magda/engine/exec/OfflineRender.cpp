#include "exec/OfflineRender.hpp"

#include <algorithm>
#include <cmath>

#include "transport/TransportState.hpp"

namespace magda::engine {
namespace {

std::int64_t samplesBetween(double startSeconds, double endSeconds, double sampleRate) {
    if (endSeconds <= startSeconds)
        return 0;
    return static_cast<std::int64_t>(std::llround((endSeconds - startSeconds) * sampleRate));
}

}  // namespace

OfflineRenderResult renderOffline(PlanExecutor& executor, const PlanValues& values,
                                  const RenderContext& context, const TempoMap& tempo,
                                  const OfflineRenderRequest& request, OfflineRenderSink& sink,
                                  const std::function<bool()>& shouldContinue) {
    OfflineRenderResult result;

    // The context is refused on the same grounds the values are, and for the
    // same reason: what the executor does with either when they do not belong
    // to it is render something plausible and say nothing. Blocks are sized and
    // the clock is advanced from the argument, while process() renders at most
    // what it was prepared for, so a longer maxBlockSize prints the head of
    // every block and silence for the rest. A sample rate that disagrees puts
    // the whole render at the wrong speed, and a channel count that does at the
    // wrong width.
    if (!executor.isPrepared() || !executor.appliesValues(values) ||
        !(executor.preparedContext() == context)) {
        result.refused = true;
        return result;
    }

    // Held to what the plan was prepared for, the way a callback is: the
    // executor asserts on a longer block, and here the same number also says
    // how much buffer there is to render into.
    const auto blockSize = std::clamp(request.blockSize, 1, context.maxBlockSize);

    // How long the range is, asked once. Deriving the length up front rather
    // than watching the cursor cross the end is what keeps the answer the same
    // at every block size: the last block is cut to land exactly on it, and no
    // block's length depends on how the ones before it were cut.
    const auto rangeSamples = samplesBetween(tempo.beatToTime(request.startBeat),
                                             tempo.beatToTime(request.endBeat), context.sampleRate);
    const auto tailSamples =
        request.tailSeconds > 0.0
            ? static_cast<std::int64_t>(std::llround(request.tailSeconds * context.sampleRate))
            : 0;

    juce::AudioBuffer<float> buffer(context.numChannels, blockSize);

    // The clock playback uses, driven by a request nobody typed. It is what
    // makes the cursor a function of the sample count rather than a sum of
    // per-block steps, which is the whole of why this renders the same at any
    // block size.
    TransportClock clock;

    TransportSnapshot snapshot;
    snapshot.tempo = tempo;

    // Both left off, and named here rather than left to the defaults, because
    // both would be wrong in a way that is hard to hear until it has shipped. A
    // loop would print the range twice into a bounce that asked for it once,
    // and the metronome is not the graph's to begin with: it is summed after
    // the plan during playback and is not summed at all here.
    snapshot.loop = {};
    snapshot.click = {};

    // Generation one, because the clock starts at zero and applies the first
    // request it has not seen. A count-in is a monitoring aid and would print
    // the beats before the range into the render.
    snapshot.request.generation = 1;
    snapshot.request.playing = true;
    snapshot.request.locate = true;
    snapshot.request.positionBeat = request.startBeat;
    snapshot.request.countInBeats = 0.0;

    const auto renderSpan = [&](std::int64_t samples) {
        while (samples > 0) {
            if (shouldContinue && !shouldContinue()) {
                result.cancelled = true;
                return;
            }

            const auto numSamples = static_cast<int>(std::min<std::int64_t>(samples, blockSize));

            // The executor clears each piece it is handed, so this covers only
            // what no piece reaches. Nothing should qualify, because a callback
            // with samples in it always yields at least one segment, and the
            // cost of being wrong about that is the previous block printed
            // twice into a file.
            buffer.clear();

            for (const auto& segment : clock.advance(snapshot, context.sampleRate, numSamples)) {
                juce::AudioBuffer<float> piece(buffer.getArrayOfWritePointers(),
                                               buffer.getNumChannels(), segment.startSample,
                                               segment.block.numSamples);
                executor.process(values, segment.block, piece);
            }

            sink.write(buffer, numSamples);

            result.samplesRendered += numSamples;
            ++result.blocksRendered;
            samples -= numSamples;
        }
    };

    // Applied before anything renders rather than by the first block, because a
    // range that rounds to no samples renders no blocks and would leave the
    // locate unapplied: the tail below would then stand at beat zero, where the
    // clock starts, instead of where the range ended. A zero-length advance
    // takes the request and does nothing else, and for a range that does render
    // it changes nothing, because the first real block would have applied the
    // same request a moment later.
    clock.advance(snapshot, context.sampleRate, 0);

    renderSpan(rangeSamples);

    if (result.cancelled || tailSamples <= 0)
        return result;

    // The tail, with the transport stopped where the range ended. A stopped
    // block is already defined as one the graph runs through without the
    // timeline moving, so reverbs and delays ring out and nothing that comes
    // after the range starts playing.
    //
    // It is exactly as long as it was asked for. Stopping at the first silent
    // block would be shorter and would sometimes be wrong: a gated or tremolo'd
    // tail goes fully silent in the middle and would be cut off there. The
    // caller has the samples and can trim what it does not want, which is a
    // decision it can make and this cannot.
    snapshot.request.generation = 2;
    snapshot.request.playing = false;
    snapshot.request.locate = false;

    renderSpan(tailSamples);

    return result;
}

}  // namespace magda::engine
