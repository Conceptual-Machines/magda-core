#include "io/FileAudioSource.hpp"

#include <algorithm>

namespace magda::engine {

FileAudioSource::FileAudioSource(AudioFileReader& reader, const ClipPlacement& placement)
    : reader_(reader), placement_(placement) {}

void FileAudioSource::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
    scratch_.setSize(context.numChannels, context.maxBlockSize, false, true, true);
}

void FileAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    out.clear();

    if (!block.playing || block.numSamples <= 0)
        return;

    // The part of the block the clip covers, as samples of it. Half-open at
    // both ends, so a clip ending exactly on a block boundary contributes
    // nothing to the block that starts there. Identical to the streaming
    // source, because it is the same clip in the same place.
    const auto first = block.sampleForTime(std::max(block.startSeconds, placement_.startSeconds));
    const auto last = block.sampleForTime(std::min(block.endSeconds, placement_.endSeconds));
    const auto count = last - first;

    if (count <= 0)
        return;

    const auto sourceStart = sourceSampleAt(std::max(block.startSeconds, placement_.startSeconds));

    // Past the end of the file is silence rather than a short block: the clip
    // says how long it is and a file that ran out inside it is a file that ran
    // out, not a reason to move what comes after.
    if (scratch_.getNumSamples() < count ||
        scratch_.getNumChannels() < static_cast<int>(out.getNumChannels()))
        scratch_.setSize(
            std::max(scratch_.getNumChannels(), static_cast<int>(out.getNumChannels())),
            std::max(scratch_.getNumSamples(), count), false, true, true);

    reader_.read(scratch_, 0, sourceStart, count);

    const auto channels =
        std::min(static_cast<int>(out.getNumChannels()), scratch_.getNumChannels());
    for (auto channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(out.getChannelPointer(static_cast<std::size_t>(channel)) +
                                              first,
                                          scratch_.getReadPointer(channel), count);
}

}  // namespace magda::engine
