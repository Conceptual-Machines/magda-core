#include "io/AudioFileReader.hpp"

#include <algorithm>

namespace magda::engine {

JuceAudioFileReader::JuceAudioFileReader(std::unique_ptr<juce::AudioFormatReader> reader)
    : reader_(std::move(reader)) {}

std::int64_t JuceAudioFileReader::lengthInSamples() const {
    return reader_ != nullptr ? reader_->lengthInSamples : 0;
}

double JuceAudioFileReader::sampleRate() const {
    return reader_ != nullptr ? reader_->sampleRate : 0.0;
}

int JuceAudioFileReader::numChannels() const {
    return reader_ != nullptr ? static_cast<int>(reader_->numChannels) : 0;
}

int JuceAudioFileReader::read(juce::AudioBuffer<float>& destination, int destinationOffset,
                              std::int64_t startSample, int numSamples) {
    if (reader_ == nullptr || numSamples <= 0)
        return 0;

    // What the file actually has from here. JUCE would pad the rest with
    // silence itself, but it would not say how much it padded, and the
    // difference between a short read and a late one is the difference between
    // the end of the file and a glitch.
    const auto available = static_cast<int>(
        std::clamp<std::int64_t>(reader_->lengthInSamples - startSample, 0, numSamples));

    if (available < numSamples)
        destination.clear(destinationOffset + available, numSamples - available);

    if (available <= 0)
        return 0;

    // Whether the decode worked is the only thing that says the destination now
    // holds audio rather than whatever was in it. A reader can fail inside the
    // length it advertises: a file truncated under us, a stream that went away
    // mid-frame. Reporting the samples as read anyway would publish undefined
    // buffer contents as material, and the stream would have no reason to
    // suspect anything, because a full chunk is exactly what success looks
    // like.
    if (!reader_->read(&destination, destinationOffset, available, startSample, true, true)) {
        destination.clear(destinationOffset, numSamples);
        return 0;
    }

    return available;
}

}  // namespace magda::engine
