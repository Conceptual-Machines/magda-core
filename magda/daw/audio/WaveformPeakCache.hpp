#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "magda/sdk/peaks/PeakData.hpp"

namespace magda {

/**
 * @brief Per-file peak cache at 64 samples/point, persisted to disk.
 *
 * Reads a file through AudioFormatReader and stores its sdk::PeakData in a .mpk. Paints read the
 * peaks instead of hitting the reader on every column.
 *
 * Thread model: an instance is immutable once constructed. Construction is
 * expected to happen on a background thread; reads are safe from any thread.
 *
 * On-disk layout (.mpk, version 1):
 *   - "MGPK" magic (4 bytes)
 *   - uint32 version
 *   - uint32 samplesPerPeak (64)
 *   - uint16 numChannels
 *   - uint16 reserved
 *   - double sampleRate
 *   - int64  sourceLengthSamples
 *   - int64  sourceFileSize         } used to invalidate the cache when the
 *   - int64  sourceFileModTimeMillis} underlying audio file changes
 *   - uint64 numBucketsPerChannel
 *   - For each channel: numBuckets * (int16 min, int16 max), normalized from
 *     [-1,1] floats by * 32767.
 */
class WaveformPeakCache {
  public:
    static constexpr int SAMPLES_PER_PEAK = sdk::kSamplesPerPeak;

    using MinMax = sdk::PeakMinMax;

    /**
     * @brief Try to load a previously-written cache for @p sourceFile from disk.
     * @return Loaded cache on success; nullptr if the file is missing, the
     *         header is invalid, or the source file's size/mtime no longer
     *         match the values stored in the header.
     */
    static std::unique_ptr<WaveformPeakCache> loadFromDisk(const juce::File& sourceFile);

    /**
     * @brief Walk @p reader, build a peak cache, and write it to disk.
     * @return Cache instance on success; nullptr on read or write failure.
     */
    static std::unique_ptr<WaveformPeakCache> computeAndWrite(const juce::File& sourceFile,
                                                              juce::AudioFormatReader& reader);

    /**
     * @brief Aggregate min/max over a half-open source-sample range.
     *
     * @p endSample is clamped to numSourceSamples; @p startSample is clamped to
     * [0, endSample]. Returns {0,0} if the clamped range is empty or the
     * channel is out of range.
     */
    MinMax getMinMaxForRange(int channel, std::int64_t startSample, std::int64_t endSample) const {
        return peaks_.getMinMaxForRange(channel, startSample, endSample);
    }

    int getNumChannels() const noexcept {
        return peaks_.numChannels();
    }
    std::int64_t getNumSourceSamples() const noexcept {
        return peaks_.numSourceSamples();
    }

    /** @brief The .mpk path @p sourceFile's peaks are cached at. */
    static juce::File getCacheFileFor(const juce::File& sourceFile);

  private:
    static juce::File getCacheRoot();

    explicit WaveformPeakCache(sdk::PeakData peaks) : peaks_(std::move(peaks)) {}

    sdk::PeakData peaks_;

    JUCE_DECLARE_NON_COPYABLE(WaveformPeakCache)
};

}  // namespace magda
