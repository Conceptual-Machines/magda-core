#include "io/AudioFileSink.hpp"

#include <utility>

namespace magda::engine {

namespace {

std::unique_ptr<juce::AudioFormat> formatFor(AudioFileFormat format) {
    if (format == AudioFileFormat::flac)
        return std::make_unique<juce::FlacAudioFormat>();
    return std::make_unique<juce::WavAudioFormat>();
}

}  // namespace

DitherMode defaultDitherFor(int bitDepth) {
    return bitDepth >= 32 ? DitherMode::none : DitherMode::tpdf;
}

std::unique_ptr<AudioFileSink> AudioFileSink::create(const juce::File& destination,
                                                     const AudioFileSpec& spec,
                                                     const RenderContext& context) {
    const auto channels = juce::jmax(1, context.numChannels);

    // 32 bits means float. A 32-bit integer file is not a thing anything asks
    // for, and FLAC refuses the depth itself, so this is the only place the two
    // ways of spending 32 bits have to be told apart.
    const auto floating = spec.bitDepth >= 32;

    auto format = formatFor(spec.format);
    if (!format->getPossibleBitDepths().contains(spec.bitDepth))
        return nullptr;

    // Replaced rather than appended to: a FileOutputStream over an existing
    // file starts at its end, so a second render into the same name would be
    // written after the first one's header.
    if (destination.existsAsFile() && !destination.deleteFile())
        return nullptr;

    auto file = std::make_unique<juce::FileOutputStream>(destination);
    if (!file->openedOk())
        return nullptr;

    std::unique_ptr<juce::OutputStream> stream = std::move(file);

    const auto options =
        juce::AudioFormatWriterOptions()
            .withSampleRate(context.sampleRate)
            .withNumChannels(channels)
            .withBitsPerSample(spec.bitDepth)
            .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral);

    auto writer = format->createWriterFor(stream, options);
    if (writer == nullptr)
        return nullptr;

    std::optional<PcmQuantiser> quantiser;
    if (!floating)
        quantiser.emplace(spec.bitDepth, channels,
                          spec.dither.value_or(defaultDitherFor(spec.bitDepth)));

    return std::unique_ptr<AudioFileSink>(
        new AudioFileSink(std::move(writer), channels, std::move(quantiser)));
}

AudioFileSink::AudioFileSink(std::unique_ptr<juce::AudioFormatWriter> writer, int numChannels,
                             std::optional<PcmQuantiser> quantiser)
    : writer_(std::move(writer)), numChannels_(numChannels), quantiser_(std::move(quantiser)) {}

AudioFileSink::~AudioFileSink() {
    close();
}

void AudioFileSink::write(const juce::AudioBuffer<float>& block, int numSamples) {
    if (failed_ || writer_ == nullptr || numSamples <= 0)
        return;

    // A block narrower than the file has no samples for the channels past its
    // own, and the writer would store whatever was in that memory. Refused, so
    // the file ends where the render stopped making sense.
    if (block.getNumChannels() < numChannels_) {
        failed_ = true;
        return;
    }

    if (!quantiser_) {
        failed_ = !writer_->writeFromFloatArrays(block.getArrayOfReadPointers(), numChannels_,
                                                 numSamples);
    } else {
        resizeCodes(numSamples);

        for (auto channel = 0; channel < numChannels_; ++channel)
            scratch_.copyFrom(channel, 0, block, channel, 0, numSamples);

        quantiser_->processToCodes(scratch_, numSamples, codeChannels_.data());

        // The integer path, which is the only one allowed after the quantiser:
        // writeFromAudioSampleBuffer would round the codes a second time
        // through INT_MAX and land some of them one low (io/PcmQuantiser.hpp).
        failed_ = !writer_->write(const_cast<const int**>(codeChannels_.data()), numSamples);
    }

    if (!failed_)
        samplesWritten_ += numSamples;
}

void AudioFileSink::resizeCodes(int numSamples) {
    if (numSamples <= codeCapacity_)
        return;

    codeCapacity_ = numSamples;
    codes_.assign(static_cast<std::size_t>(numChannels_) * static_cast<std::size_t>(numSamples), 0);

    // One past the channels, left null: JUCE's own writers stop at a null
    // pointer as well as at the channel count.
    codeChannels_.assign(static_cast<std::size_t>(numChannels_) + 1, nullptr);
    for (auto channel = 0; channel < numChannels_; ++channel)
        codeChannels_[static_cast<std::size_t>(channel)] =
            codes_.data() + (static_cast<std::ptrdiff_t>(channel) * numSamples);

    scratch_.setSize(numChannels_, numSamples, false, false, true);
}

bool AudioFileSink::close() {
    // Destroying the writer is what finishes the file: it writes the header it
    // could not write until the length was known, and flushes the encoder.
    writer_.reset();
    return !failed_;
}

}  // namespace magda::engine
