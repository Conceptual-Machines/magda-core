#include "io/Mp3AudioFormat.hpp"

#include <lame.h>

#include <array>
#include <utility>
#include <vector>

namespace magda::engine {

namespace {

// MPEG-1 Layer III bitrates in kbps (ISO 11172-3, 2.4.2.3), free format left out.
constexpr std::array<int, 14> kCbrBitratesKbps = {32,  40,  48,  56,  64,  80,  96,
                                                  112, 128, 160, 192, 224, 256, 320};

constexpr int kVbrLevels = 10;
constexpr int kAutoVbrLevel = 2;

/// LAME's own bound on what one call can emit (lame.h, lame_encode_buffer).
int outputBoundFor(int numSamples) {
    return (numSamples * 5 / 4) + 7200;
}

using Encoder = std::unique_ptr<lame_global_flags, decltype(&lame_close)>;

/// The encoder for @p options, or null if LAME refuses them. Quality indices
/// follow Mp3AudioFormat::getQualityOptions.
Encoder createEncoder(const juce::AudioFormatWriterOptions& options) {
    Encoder encoder(lame_init(), &lame_close);
    if (encoder == nullptr)
        return {nullptr, &lame_close};

    auto* flags = encoder.get();
    lame_set_in_samplerate(flags, static_cast<int>(options.getSampleRate()));
    lame_set_out_samplerate(flags, mp3FileRateFor(options.getSampleRate()));
    lame_set_num_channels(flags, options.getNumChannels());

    // Tags are written by MAGDA from the project's credits (#3035), not by LAME.
    lame_set_write_id3tag_automatic(flags, 0);

    const auto index = options.getQualityOptionIndex();
    if (index <= kVbrLevels) {
        lame_set_VBR(flags, vbr_default);
        lame_set_VBR_quality(flags, static_cast<float>(index == 0 ? kAutoVbrLevel : index - 1));
    } else {
        const auto cbr = static_cast<std::size_t>(index - kVbrLevels - 1);
        if (cbr >= kCbrBitratesKbps.size())
            return {nullptr, &lame_close};
        lame_set_VBR(flags, vbr_off);
        lame_set_brate(flags, kCbrBitratesKbps[cbr]);
    }

    if (lame_init_params(flags) < 0)
        return {nullptr, &lame_close};
    return encoder;
}

class Mp3Writer final : public juce::AudioFormatWriter {
  public:
    Mp3Writer(juce::OutputStream* out, Encoder encoder, int numChannels)
        : juce::AudioFormatWriter(out, "MP3", lame_get_out_samplerate(encoder.get()),
                                  static_cast<unsigned int>(numChannels), 32),
          encoder_(std::move(encoder)) {
        usesFloatingPointData = true;
    }

    ~Mp3Writer() override {
        encoded_.resize(static_cast<std::size_t>(outputBoundFor(0)));
        const auto flushed =
            lame_encode_flush(encoder_.get(), encoded_.data(), static_cast<int>(encoded_.size()));
        if (flushed > 0)
            output->write(encoded_.data(), static_cast<std::size_t>(flushed));

        // The LAME/Xing frame at the front, rewritten now the frame count and
        // the encoder's delay and padding are known: what gives players the
        // duration of a VBR file and lets gapless decoders trim the padding.
        std::array<unsigned char, 2880> tag{};
        const auto tagSize = lame_get_lametag_frame(encoder_.get(), tag.data(), tag.size());
        if (tagSize > 0 && tagSize <= tag.size() && output->setPosition(0))
            output->write(tag.data(), tagSize);

        output->flush();
    }

    Mp3Writer(const Mp3Writer&) = delete;
    Mp3Writer& operator=(const Mp3Writer&) = delete;
    Mp3Writer(Mp3Writer&&) = delete;
    Mp3Writer& operator=(Mp3Writer&&) = delete;

    /// @p samplesToWrite holds float: usesFloatingPointData is set.
    bool write(const int** samplesToWrite, int numSamples) override {
        if (numSamples <= 0)
            return true;

        const auto* left = reinterpret_cast<const float*>(samplesToWrite[0]);
        const auto* right = numChannels > 1 && samplesToWrite[1] != nullptr
                                ? reinterpret_cast<const float*>(samplesToWrite[1])
                                : left;

        encoded_.resize(static_cast<std::size_t>(outputBoundFor(numSamples)));
        const auto written =
            lame_encode_buffer_ieee_float(encoder_.get(), left, right, numSamples, encoded_.data(),
                                          static_cast<int>(encoded_.size()));
        if (written < 0)
            return false;
        return written == 0 || output->write(encoded_.data(), static_cast<std::size_t>(written));
    }

  private:
    Encoder encoder_;
    std::vector<unsigned char> encoded_;
};

}  // namespace

int mp3FileRateFor(double renderRate) {
    constexpr std::array<int, 9> kRates = {8000,  11025, 12000, 16000, 22050,
                                           24000, 32000, 44100, 48000};
    const auto rate = juce::roundToInt(renderRate);
    if (rate > 48000)
        return rate % 44100 == 0 ? 44100 : 48000;
    for (const auto candidate : kRates)
        if (candidate >= rate)
            return candidate;
    return 48000;
}

Mp3AudioFormat::Mp3AudioFormat() : juce::AudioFormat("MP3", ".mp3") {}

juce::Array<int> Mp3AudioFormat::getPossibleSampleRates() {
    return {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
}

juce::Array<int> Mp3AudioFormat::getPossibleBitDepths() {
    return {32};
}

bool Mp3AudioFormat::canDoStereo() {
    return true;
}

bool Mp3AudioFormat::canDoMono() {
    return true;
}

bool Mp3AudioFormat::isCompressed() {
    return true;
}

juce::StringArray Mp3AudioFormat::getQualityOptions() {
    juce::StringArray options{"Auto"};
    for (auto level = 0; level < kVbrLevels; ++level)
        options.add("V" + juce::String(level));
    for (const auto kbps : kCbrBitratesKbps)
        options.add(juce::String(kbps) + " kbps");
    return options;
}

juce::AudioFormatReader* Mp3AudioFormat::createReaderFor(juce::InputStream* sourceStream,
                                                         bool deleteStreamIfOpeningFails) {
    if (deleteStreamIfOpeningFails)
        delete sourceStream;
    return nullptr;
}

std::unique_ptr<juce::AudioFormatWriter> Mp3AudioFormat::createWriterFor(
    std::unique_ptr<juce::OutputStream>& streamToWriteTo,
    const juce::AudioFormatWriterOptions& options) {
    const auto channels = options.getNumChannels();
    if (streamToWriteTo == nullptr || channels < 1 || channels > 2 || options.getSampleRate() <= 0)
        return nullptr;

    auto encoder = createEncoder(options);
    if (encoder == nullptr)
        return nullptr;

    return std::make_unique<Mp3Writer>(std::exchange(streamToWriteTo, {}).release(),
                                       std::move(encoder), channels);
}

}  // namespace magda::engine
