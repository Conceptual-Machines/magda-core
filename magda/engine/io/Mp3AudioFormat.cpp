// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// @p text as LAME's UTF-16 setters take it: byte-order mark first, then null-terminated.
std::vector<unsigned short> utf16For(const juce::String& text) {
    std::vector<unsigned short> result{0xFEFF};
    for (auto unit = text.toUTF16(); !unit.isEmpty(); ++unit) {
        const auto codePoint = *unit;
        if (codePoint < 0x10000) {
            result.push_back(static_cast<unsigned short>(codePoint));
        } else {
            const auto offset = codePoint - 0x10000;
            result.push_back(static_cast<unsigned short>(0xD800 + (offset >> 10)));
            result.push_back(static_cast<unsigned short>(0xDC00 + (offset & 0x3FF)));
        }
    }
    result.push_back(0);
    return result;
}

/// The credits in @p options as ID3v2 frames, UTF-16 throughout so a name
/// outside Latin-1 survives. Nothing is set, and no tag written, without them.
void setTags(lame_global_flags* flags, const juce::AudioFormatWriterOptions& options) {
    const auto metadata = options.getMetadataValues();
    const std::pair<const char*, const char*> frames[] = {
        {juce::OpusAudioFormat::id3title, "TIT2"},
        {juce::OpusAudioFormat::id3artist, "TPE1"},
        {juce::OpusAudioFormat::id3album, "TALB"},
        {juce::OpusAudioFormat::id3date, "TYER"},
        {juce::OpusAudioFormat::id3genre, "TCON"}};

    auto initialised = false;
    const auto init = [&] {
        if (std::exchange(initialised, true))
            return;
        id3tag_init(flags);
        id3tag_v2_only(flags);
    };

    for (const auto& [key, frame] : frames)
        if (const auto it = metadata.find(key); it != metadata.end() && it->second.isNotEmpty()) {
            init();
            id3tag_set_textinfo_utf16(flags, frame, utf16For(it->second).data());
        }

    if (const auto it = metadata.find(juce::OpusAudioFormat::id3comment);
        it != metadata.end() && it->second.isNotEmpty()) {
        init();
        id3tag_set_comment_utf16(flags, "eng", utf16For({}).data(), utf16For(it->second).data());
    }
}

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

    // The ID3v2 tag is fetched and written by the writer, so it knows where the
    // first frame, and so the LAME tag, starts.
    lame_set_write_id3tag_automatic(flags, 0);
    setTags(flags, options);

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

        // Zero when no tags were set. A failed write here surfaces where every
        // other one does, in the sink's stream report.
        const auto tagSize = lame_get_id3v2_tag(encoder_.get(), nullptr, 0);
        if (tagSize > 0) {
            std::vector<unsigned char> tag(tagSize);
            if (lame_get_id3v2_tag(encoder_.get(), tag.data(), tag.size()) == tagSize &&
                output->write(tag.data(), tagSize))
                firstFrame_ = static_cast<juce::int64>(tagSize);
        }
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
        if (tagSize > 0 && tagSize <= tag.size() && output->setPosition(firstFrame_))
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

    /// Where the audio starts: after the ID3v2 tag, where the LAME tag goes.
    juce::int64 firstFrame_ = 0;
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
