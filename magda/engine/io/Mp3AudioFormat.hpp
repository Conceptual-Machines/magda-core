#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/**
 * @file Mp3AudioFormat.hpp
 * @brief An MP3 writer over the vendored libmp3lame (#3033).
 *
 * JUCE reads MP3 but does not write it: its LAMEEncoderAudioFormat runs a
 * `lame` executable. This writes in process, one or two channels, and never
 * reads (createReaderFor is null; juce::MP3AudioFormat does that).
 *
 * The writer takes float, so a render above full scale reaches the encoder
 * unclipped rather than rounded through int32 first.
 */

namespace magda::engine {

/// The rate an MP3 of a @p renderRate render is stored at. MP3 stops at 48 kHz,
/// so a higher rate is resampled by the encoder to the base rate of its own
/// family: 88.2 and 176.4 kHz become 44.1, everything else above 48 becomes 48.
int mp3FileRateFor(double renderRate);

class Mp3AudioFormat final : public juce::AudioFormat {
  public:
    Mp3AudioFormat();

    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths() override;
    bool canDoStereo() override;
    bool canDoMono() override;
    bool isCompressed() override;

    /// "Auto" (VBR at V2), then "V0" to "V9" (VBR, best first), then CBR as
    /// "<n> kbps" at each MPEG-1 Layer III bitrate from 32 to 320.
    juce::StringArray getQualityOptions() override;

    juce::AudioFormatReader* createReaderFor(juce::InputStream* sourceStream,
                                             bool deleteStreamIfOpeningFails) override;

    using juce::AudioFormat::createWriterFor;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(
        std::unique_ptr<juce::OutputStream>& streamToWriteTo,
        const juce::AudioFormatWriterOptions& options) override;
};

}  // namespace magda::engine
