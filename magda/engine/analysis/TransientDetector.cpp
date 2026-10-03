#include "analysis/TransientDetector.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

namespace magda::engine {

std::vector<double> detectTransients(AudioFileReader& reader,
                                     const TransientDetectionSettings& settings) {
    // The reader fills a one-channel view over the detector's block.
    const auto read = [&reader](float* destination, std::int64_t start, int count) {
        float* channels[1] = {destination};
        juce::AudioBuffer<float> block(channels, 1, count);
        return reader.read(block, 0, start, count);
    };
    return sdk::detectTransients(read, reader.lengthInSamples(), reader.sampleRate(), settings);
}

}  // namespace magda::engine
