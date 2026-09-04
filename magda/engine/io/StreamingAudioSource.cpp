#include "io/StreamingAudioSource.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

StreamingAudioSource::StreamingAudioSource(PrefetchStream& stream, const ClipPlacement& placement)
    : stream_(stream), placement_(placement) {}

void StreamingAudioSource::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
}

void StreamingAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    out.clear();

    // Every block, sounding or not. A clip that has not started is exactly what
    // a cue is for, and a stream nobody reads from would not hear about one
    // until the material it was cued for was already due.
    stream_.applyPendingCue();

    if (!block.playing || block.numSamples <= 0)
        return;

    // The part of the block the clip covers, as samples of it. Half-open at
    // both ends, so a clip ending exactly on a block boundary contributes
    // nothing to the block that starts there.
    const auto first = block.edgeForTime(std::max(block.seconds.start, placement_.seconds.start));
    const auto last = block.edgeForTime(std::min(block.seconds.end, placement_.seconds.end));
    const auto count = last - first;

    if (count <= 0)
        return;

    // Where in the file that part begins. Derived from the timeline rather than
    // from where the last block left off, so a jump needs nothing said about
    // it: the stream sees a position it was not expecting and seeks, which is
    // the same path a loop wrap takes.
    const auto sourceStart =
        sourceSampleAt(std::max(block.seconds.start, placement_.seconds.start));

    stream_.read(
        sourceStart,
        out.getSubBlock(static_cast<std::size_t>(first.value), static_cast<std::size_t>(count)),
        count);
}

}  // namespace magda::engine
