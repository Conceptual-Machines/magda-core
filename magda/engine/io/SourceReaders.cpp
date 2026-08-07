#include "io/SourceReaders.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace magda::engine {

namespace {

/// A read that answers a position before the first sample with silence rather
/// than with whatever a reader does when it is asked for one. The resampler
/// asks on purpose, because the curve it interpolates along needs the sample
/// before the one it lands on, and a loop region that runs off the front of its
/// own file can put a tile there too.
int readFrom(AudioFileReader& file, juce::AudioBuffer<float>& destination, int destinationOffset,
             std::int64_t startSample, int numSamples) {
    if (numSamples <= 0)
        return 0;

    const auto silent = static_cast<int>(std::clamp<std::int64_t>(-startSample, 0, numSamples));
    if (silent > 0)
        destination.clear(destinationOffset, silent);
    if (silent == numSamples)
        return 0;

    return file.read(destination, destinationOffset + silent, startSample + silent,
                     numSamples - silent);
}

/// Cubic Lagrange through four samples, @p t of the way from the second to the
/// third.
float interpolate(float first, float second, float third, float fourth, double t) {
    const auto a = -t * (t - 1.0) * (t - 2.0) / 6.0;
    const auto b = (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0;
    const auto c = -(t + 1.0) * t * (t - 2.0) / 2.0;
    const auto d = (t + 1.0) * t * (t - 1.0) / 6.0;

    return static_cast<float>(a * first + b * second + c * third + d * fourth);
}

}  // namespace

std::unique_ptr<AudioFileReader> readThrough(std::unique_ptr<AudioFileReader> file,
                                             const SourceRead& how) {
    if (file == nullptr)
        return file;

    if (how.reversed)
        file = std::make_unique<ReversedAudioFileReader>(std::move(file), how.lengthInSamples);

    if (how.loopLengthSamples > 0)
        file = std::make_unique<LoopingAudioFileReader>(std::move(file), how.loopStartSamples,
                                                        how.loopLengthSamples);

    // A rate that already matches gets no layer, and neither does one nothing
    // knows: a source no probe has resolved plays as the device's rate rather
    // than being resampled by a guess.
    if (how.sourceSampleRate > 0.0 && how.deviceSampleRate > 0.0 &&
        std::abs(how.sourceSampleRate - how.deviceSampleRate) > 1.0e-6)
        file = std::make_unique<ResamplingAudioFileReader>(std::move(file), how.sourceSampleRate,
                                                           how.deviceSampleRate);

    return file;
}

// ---------------------------------------------------------------------------

ReversedAudioFileReader::ReversedAudioFileReader(std::unique_ptr<AudioFileReader> file,
                                                 std::int64_t lengthInSamples)
    : file_(std::move(file)), length_(std::max<std::int64_t>(0, lengthInSamples)) {}

double ReversedAudioFileReader::sampleRate() const {
    return file_ != nullptr ? file_->sampleRate() : 0.0;
}

int ReversedAudioFileReader::numChannels() const {
    return file_ != nullptr ? file_->numChannels() : 0;
}

int ReversedAudioFileReader::read(juce::AudioBuffer<float>& destination, int destinationOffset,
                                  std::int64_t startSample, int numSamples) {
    if (file_ == nullptr || numSamples <= 0)
        return 0;

    // Past the end of this is past the beginning of the file, and there is
    // nothing there either way.
    const auto available =
        static_cast<int>(std::clamp<std::int64_t>(length_ - startSample, 0, numSamples));

    if (available < numSamples)
        destination.clear(destinationOffset + available, numSamples - available);
    if (available <= 0)
        return 0;

    // The run [start, start + available) is the file's [length - start -
    // available, length - start), turned round. A position before the first
    // sample of this is past the last sample of the file, and comes back as the
    // silence the file has there; the flip puts it where it belongs, in front
    // of the material rather than behind it.
    const auto found =
        file_->read(destination, destinationOffset, length_ - startSample - available, available);

    for (auto channel = 0; channel < destination.getNumChannels(); ++channel) {
        auto* samples = destination.getWritePointer(channel) + destinationOffset;
        std::reverse(samples, samples + available);
    }

    // Every one of these positions exists, whatever the file had at the far end
    // of them. Reporting the file's count would say the material starts where
    // the flip has just put the silence.
    return found > 0 ? available : 0;
}

// ---------------------------------------------------------------------------

LoopingAudioFileReader::LoopingAudioFileReader(std::unique_ptr<AudioFileReader> file,
                                               std::int64_t loopStartSamples,
                                               std::int64_t loopLengthSamples)
    : file_(std::move(file)),
      loopStart_(loopStartSamples),
      loopLength_(std::max<std::int64_t>(0, loopLengthSamples)) {}

std::int64_t LoopingAudioFileReader::lengthInSamples() const {
    return std::numeric_limits<std::int64_t>::max();
}

double LoopingAudioFileReader::sampleRate() const {
    return file_ != nullptr ? file_->sampleRate() : 0.0;
}

int LoopingAudioFileReader::numChannels() const {
    return file_ != nullptr ? file_->numChannels() : 0;
}

int LoopingAudioFileReader::read(juce::AudioBuffer<float>& destination, int destinationOffset,
                                 std::int64_t startSample, int numSamples) {
    if (file_ == nullptr || numSamples <= 0)
        return 0;

    if (loopLength_ <= 0)
        return readFrom(*file_, destination, destinationOffset, startSample, numSamples);

    const auto loopEnd = loopStart_ + loopLength_;

    auto done = 0;
    auto material = false;

    while (done < numSamples) {
        // Every position is inside the region, including the ones in front of
        // it: an event anchored before its own loop start is anchored at a
        // phase within the loop, which is how the incumbent reads one too.
        const auto offset = (startSample + done) - loopStart_;
        const auto phase = offset % loopLength_;
        const auto found = loopStart_ + (phase < 0 ? phase + loopLength_ : phase);

        const auto run =
            static_cast<int>(std::min<std::int64_t>(numSamples - done, loopEnd - found));
        if (run <= 0)
            break;

        if (readFrom(*file_, destination, destinationOffset + done, found, run) > 0)
            material = true;

        done += run;
    }

    if (done < numSamples)
        destination.clear(destinationOffset + done, numSamples - done);

    // Silence inside the region is material: a loop over a region the file does
    // not reach plays that silence every time round, and calling it the end of
    // the file would leave the stream above asking for it again instead of
    // moving on. A read with nothing anywhere in it is the other thing, and
    // says so, because a reader that has stopped answering has to be visible.
    return material ? numSamples : 0;
}

// ---------------------------------------------------------------------------

ResamplingAudioFileReader::ResamplingAudioFileReader(std::unique_ptr<AudioFileReader> file,
                                                     double sourceSampleRate,
                                                     double deviceSampleRate)
    : file_(std::move(file)), deviceSampleRate_(deviceSampleRate) {
    if (sourceSampleRate > 0.0 && deviceSampleRate > 0.0)
        ratio_ = sourceSampleRate / deviceSampleRate;

    const auto source = file_ != nullptr ? file_->lengthInSamples() : 0;

    // A source with no end has none at this rate either: a loop underneath has
    // no last sample to convert.
    length_ = source > std::numeric_limits<std::int64_t>::max() / 2
                  ? source
                  : static_cast<std::int64_t>(std::ceil(static_cast<double>(source) / ratio_));
}

int ResamplingAudioFileReader::numChannels() const {
    return file_ != nullptr ? file_->numChannels() : 0;
}

int ResamplingAudioFileReader::read(juce::AudioBuffer<float>& destination, int destinationOffset,
                                    std::int64_t startSample, int numSamples) {
    if (file_ == nullptr || numSamples <= 0)
        return 0;

    const auto available =
        static_cast<int>(std::clamp<std::int64_t>(length_ - startSample, 0, numSamples));

    if (available < numSamples)
        destination.clear(destinationOffset + available, numSamples - available);
    if (available <= 0)
        return 0;

    // The source samples this read lands between, and the one either side that
    // the curve reaches for.
    const auto first =
        static_cast<std::int64_t>(std::floor(static_cast<double>(startSample) * ratio_)) - 1;
    const auto last = static_cast<std::int64_t>(
                          std::floor(static_cast<double>(startSample + available - 1) * ratio_)) +
                      2;
    const auto count = static_cast<int>(last - first + 1);

    const auto channels = destination.getNumChannels();
    if (window_.getNumChannels() < channels || window_.getNumSamples() < count)
        window_.setSize(std::max(channels, window_.getNumChannels()),
                        std::max(count, window_.getNumSamples()), false, true, false);

    if (readFrom(*file_, window_, 0, first, count) <= 0) {
        destination.clear(destinationOffset, available);
        return 0;
    }

    for (auto channel = 0; channel < channels; ++channel) {
        const auto* source = window_.getReadPointer(channel);
        auto* out = destination.getWritePointer(channel) + destinationOffset;

        for (auto sample = 0; sample < available; ++sample) {
            // From the output sample's own position, so that this read and any
            // other read covering it agree exactly.
            const auto position =
                static_cast<double>(startSample + sample) * ratio_ - static_cast<double>(first);
            const auto index = static_cast<int>(std::floor(position));

            out[sample] = interpolate(source[index - 1], source[index], source[index + 1],
                                      source[index + 2], position - index);
        }
    }

    return available;
}

}  // namespace magda::engine
