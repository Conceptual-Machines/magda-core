#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>

/**
 * @file AudioFileReader.hpp
 * @brief Random access to a file's samples, off the audio thread.
 *
 * The narrowest thing the prefetcher needs, and deliberately not a file: what
 * decodes a format, where the file came from and how it was opened are the
 * host's business. The engine is handed something that can already read.
 *
 * That is also what makes the prefetcher testable without a disk. A test reader
 * that computes its samples exercises every path the real one does, and does it
 * deterministically, which reading a fixture off a filesystem does not.
 */

namespace magda::engine {

class AudioFileReader {
  public:
    virtual ~AudioFileReader() = default;

    /// Samples in the file, per channel. Constant for the file's lifetime.
    virtual std::int64_t lengthInSamples() const = 0;

    /// The rate the file was recorded at, which is not necessarily the rate the
    /// device runs at. Converting between them is the clip layer's problem
    /// (#1890): what is delivered here is the file's own samples.
    virtual double sampleRate() const = 0;

    virtual int numChannels() const = 0;

    /**
     * @brief Read @p numSamples into @p destination from @p destinationOffset.
     *
     * Called on the prefetch thread, never on the audio thread. Returns how
     * many samples were actually available, which is short only at the end of
     * the file; the rest of the destination is cleared rather than left
     * holding whatever was there.
     *
     * The destination carries the engine's channel count rather than the
     * file's: a mono file fans out to every channel, and a file with more
     * channels than the engine renders contributes its first two, because the
     * model has no way to say what else it should do with them.
     */
    virtual int read(juce::AudioBuffer<float>& destination, int destinationOffset,
                     std::int64_t startSample, int numSamples) = 0;
};

/**
 * @brief An AudioFileReader over a JUCE reader the host has already opened.
 *
 * Which formats exist, where the file is and whether it is still there are all
 * settled before this is constructed. Owns the reader it is given.
 */
class JuceAudioFileReader final : public AudioFileReader {
  public:
    explicit JuceAudioFileReader(std::unique_ptr<juce::AudioFormatReader> reader);

    std::int64_t lengthInSamples() const override;
    double sampleRate() const override;
    int numChannels() const override;
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override;

  private:
    std::unique_ptr<juce::AudioFormatReader> reader_;
};

}  // namespace magda::engine
