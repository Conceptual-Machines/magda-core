#include "io/StreamingAudioSource.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

StreamingAudioSource::StreamingAudioSource(PrefetchStream& stream, const ClipPlacement& placement)
    : stream_(stream), placement_(placement) {}

void StreamingAudioSource::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
}

std::int64_t StreamingAudioSource::sourceSampleAt(double seconds) const {
    // Counted in the file's own samples, from the moment its first sample
    // plays. The rate is the file's rather than the device's, so a locate lands
    // on the right sample of the material even when the two differ.
    const auto rate = stream_.sampleRate() > 0.0 ? stream_.sampleRate() : sampleRate_;
    return placement_.sourceOffsetSamples +
           static_cast<std::int64_t>(std::llround((seconds - placement_.startSeconds) * rate));
}

void StreamingAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    out.clear();

    if (!block.playing || block.numSamples <= 0)
        return;

    // The part of the block the clip covers, as samples of it. Half-open at
    // both ends, so a clip ending exactly on a block boundary contributes
    // nothing to the block that starts there.
    const auto first = block.sampleForTime(std::max(block.startSeconds, placement_.startSeconds));
    const auto last = block.sampleForTime(std::min(block.endSeconds, placement_.endSeconds));
    const auto count = last - first;

    if (count <= 0)
        return;

    // Where in the file that part begins. Derived from the timeline rather than
    // from where the last block left off, so a jump needs nothing said about
    // it: the stream sees a position it was not expecting and seeks, which is
    // the same path a loop wrap takes.
    const auto sourceStart = sourceSampleAt(std::max(block.startSeconds, placement_.startSeconds));

    stream_.read(sourceStart,
                 out.getSubBlock(static_cast<std::size_t>(first), static_cast<std::size_t>(count)),
                 count);
}

}  // namespace magda::engine
