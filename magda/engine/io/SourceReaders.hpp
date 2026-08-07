#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <memory>

#include "io/AudioFileReader.hpp"

/**
 * @file SourceReaders.hpp
 * @brief The file an event reads, which is not always the file on the disk.
 *
 * Three of the things a clip asks of its material are not processing at all.
 * They are which of the file's samples answer a position: playing backwards
 * mirrors the file, looping tiles a region of it, and a file recorded at a rate
 * the device does not run at has its samples in the wrong places. Each of them
 * is a reader wrapped around a reader, so what the prefetcher, the pool and the
 * voice see is one file, read forwards, at the device's rate.
 *
 * That is also the answer to how reverse plays. A prefetch stream reads ahead
 * of the callback and can only be pointed at one place, so a voice asking for
 * descending blocks would leave the reader somewhere it was not pointed every
 * block, which is a seek per block and silence for the whole take (#2016).
 * Mirroring the file underneath leaves the stream reading forwards through a
 * file whose samples are in the other order, and the flip is done by the thread
 * that is allowed to wait for a disk rather than by the one that is not. The
 * incumbent renders a reversed copy of the file to disk for this; what is
 * different here is that nothing is written and nothing has to finish before
 * the clip can play.
 *
 * They compose in the incumbent's order: mirror, then tile, then convert.
 * Tiling over the mirror is what makes a reversed loop the same region played
 * backwards, which is what its reversed loop points say. Converting last leaves
 * everything below it in the source's own samples, where the model holds them,
 * and everything above it in the device's, where a stretch whose ratio changes
 * with the tempo will have to live (#2037).
 *
 * All of this runs on the prefetch thread. Reading a file is what these are
 * for, so no callback may reach them, and the working buffers they need are
 * allocated where they are used.
 */

namespace magda::engine {

/**
 * @brief What an event asks of its file, beyond the samples themselves.
 *
 * In the source's own samples at the source's own rate, which is how the model
 * holds a source-domain position; converting them before they get here would be
 * converting them twice.
 *
 * Already mirrored where the read is reversed. Which region of a mirrored file
 * a loop covers is a question about the event rather than about the reader, and
 * clip/EventPlacement.hpp answers it once for both this and the position a
 * voice reads at.
 */
struct SourceRead {
    /// Samples in the source, as the model counts them, and the axis a mirrored
    /// read turns about. The model's count rather than the file's on purpose:
    /// every other source-domain value in a snapshot is expressed against it,
    /// so a file that has gained a sample under the project moves nothing.
    std::int64_t lengthInSamples = 0;

    double sourceSampleRate = 0.0;
    double deviceSampleRate = 0.0;

    bool reversed = false;

    /// The region that repeats to fill the event. A length of zero is no
    /// tiling, which is every clip that is not looping.
    std::int64_t loopStartSamples = 0;
    std::int64_t loopLengthSamples = 0;

    bool operator==(const SourceRead&) const = default;
};

/**
 * @brief @p file, presented the way @p how asks for it.
 *
 * The file itself when nothing is asked of it, so an ordinary clip at the
 * device's rate reads through no extra layer at all and pays for none.
 */
std::unique_ptr<AudioFileReader> readThrough(std::unique_ptr<AudioFileReader> file,
                                             const SourceRead& how);

/**
 * @brief A file, back to front.
 *
 * Sample i of this is sample length - 1 - i of the file, so a run forwards
 * through here is a run backwards through the file, flipped once it has been
 * read. Everything above it goes on reading forwards and knows nothing about
 * it, which is the whole point (see the file comment).
 *
 * The length is given rather than taken from the file underneath, because it is
 * the axis every mirrored position was worked out against. Taking it from the
 * file would let a stale source table mirror the material about one point and
 * place the clip against another.
 */
class ReversedAudioFileReader final : public AudioFileReader {
  public:
    ReversedAudioFileReader(std::unique_ptr<AudioFileReader> file, std::int64_t lengthInSamples);

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override;
    int numChannels() const override;
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override;

  private:
    std::unique_ptr<AudioFileReader> file_;
    std::int64_t length_ = 0;
};

/**
 * @brief A region of a file, repeating.
 *
 * One event tiling its own source, which is what a looped clip is: the region
 * plays, and when it runs out it plays again. Every position is inside it,
 * including the ones outside its bounds, because an anchor away from the loop
 * start is a phase within the loop rather than a place of its own. That is the
 * incumbent's reading too: its clips carry a loop phase where they carry an
 * offset otherwise.
 *
 * Endless, and that is not a figure of speech: there is no last sample of a
 * loop, so nothing here ever reports the end of a file. What crops it is the
 * clip's span, which is where the cropping belongs.
 *
 * A tile boundary is a discontinuity in the file and nowhere else. The stream
 * above reads on through it without noticing, so a wrap costs no seek and no
 * block of silence, which is what a loop being a reader rather than a jump
 * buys (PrefetchStream::seek says why it could not be one before).
 */
class LoopingAudioFileReader final : public AudioFileReader {
  public:
    LoopingAudioFileReader(std::unique_ptr<AudioFileReader> file, std::int64_t loopStartSamples,
                           std::int64_t loopLengthSamples);

    /// No end, so nothing above ever asks whether there is one.
    std::int64_t lengthInSamples() const override;
    double sampleRate() const override;
    int numChannels() const override;
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override;

  private:
    std::unique_ptr<AudioFileReader> file_;
    std::int64_t loopStart_ = 0;
    std::int64_t loopLength_ = 0;
};

/**
 * @brief A file at the device's rate, whatever rate it was recorded at.
 *
 * The layer ClipPlacement.hpp leaves room for. Positions above this are device
 * samples, one consumed per output sample, which is what keeps the prefetcher
 * in step with the callback; positions below it are the file's own samples,
 * which is how the model counts an anchor and a loop. A file at the device's
 * rate does not get one of these at all.
 *
 * The source position of an output sample is that sample's own position times
 * the ratio and nothing else. No cursor, no carried phase, no interpolator
 * state: two reads that overlap agree to the last bit, a seek lands exactly
 * where reading through would have, and an offline render of the same clip
 * resolves the same samples as the callback did. State would buy a fraction of
 * a sample of accuracy at the price of a bounce that could disagree with what
 * was heard.
 *
 * Cubic Lagrange, which is the family the incumbent's default resampling
 * quality is from. Its sinc settings are a quality option rather than a
 * behaviour, and there is nothing in the model to select one with yet (#1890).
 */
class ResamplingAudioFileReader final : public AudioFileReader {
  public:
    ResamplingAudioFileReader(std::unique_ptr<AudioFileReader> file, double sourceSampleRate,
                              double deviceSampleRate);

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return deviceSampleRate_;
    }
    int numChannels() const override;
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override;

  private:
    std::unique_ptr<AudioFileReader> file_;

    /// Source samples per device sample. Above one for a file recorded higher
    /// than the device runs.
    double ratio_ = 1.0;
    double deviceSampleRate_ = 0.0;
    std::int64_t length_ = 0;

    /// The source samples one read interpolates from, plus the sample either
    /// side that the curve needs. Grown to fit on the prefetch thread, which is
    /// allowed to allocate, and stable after the first read of a given size.
    juce::AudioBuffer<float> window_;
};

}  // namespace magda::engine
