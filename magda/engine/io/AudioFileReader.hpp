#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <string>

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
     * One consumer per reader, on any thread that is not the audio thread. The
     * prefetch thread is one such consumer and an offline render is another,
     * but never through the same instance: a read is a seek and a read
     * together, so two of them interleaving leaves both somewhere neither
     * asked for. Locking here instead would put a bounce and the prefetch
     * thread in each other's way, which is the wrong trade for a file that
     * could simply have been opened twice.
     *
     * Returns how many samples were actually available, which is short only at
     * the end of the file; the rest of the destination is cleared rather than
     * left holding whatever was there.
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

/**
 * @brief Opens the files a snapshot names.
 *
 * Implemented by the host for the same reason the reader is an interface: which
 * formats exist and where a path points are the host's business, and the engine
 * is handed something that can already read. A snapshot carries paths because
 * that is what the model has; turning one into a reader happens here.
 *
 * One reader per call, never a shared one. A reader is a position as well as a
 * file, so two clips over one file need two of them (AudioFileReader::read).
 */
class AudioFileReaderFactory {
  public:
    virtual ~AudioFileReaderFactory() = default;

    /**
     * @brief A reader over @p path, or null when there is not one to be had.
     *
     * Off the audio thread, and allowed to take as long as opening a file
     * takes: whoever calls this is a thread that may wait for a disk.
     *
     * Null is the ordinary answer for a file that has moved, been deleted or
     * cannot be decoded. The caller reports it and plays silence; nothing here
     * throws and nothing retries behind the caller's back.
     */
    virtual std::unique_ptr<AudioFileReader> open(const std::string& path) = 0;
};

}  // namespace magda::engine
