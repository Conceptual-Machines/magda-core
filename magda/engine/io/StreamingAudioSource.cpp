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
    // At the device's rate, not the file's, and that is not a slip. One file
    // sample is copied per output sample below, so a position derived at any
    // other rate would disagree with what playing actually consumes: every
    // block would land somewhere the stream was not expecting, which is a seek,
    // and a file at the wrong rate would spend every callback re-pointing the
    // reader and hearing nothing. Deriving it at the device's rate keeps the
    // two in step, and what a mismatched file costs is then pitch rather than
    // playback.
    return placement_.sourceOffsetSamples + static_cast<std::int64_t>(std::llround(
                                                (seconds - placement_.startSeconds) * sampleRate_));
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
