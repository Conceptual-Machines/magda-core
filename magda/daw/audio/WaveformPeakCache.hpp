#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace magda {

/**
 * @brief Per-file peak cache at 64 samples/point, persisted to disk.
 *
 * Mirrors the spirit of Ableton's `.asd` files: the smooth waveform renderer
 * needs finer-than-thumbnail resolution but does not need raw samples, so we
 * pre-compute and store min/max pairs once per source file. Subsequent paints
 * read from this cache instead of hitting AudioFormatReader on every column.
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
    static constexpr int SAMPLES_PER_PEAK = 64;

    struct MinMax {
        float min = 0.0f;
        float max = 0.0f;
    };

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
    MinMax getMinMaxForRange(int channel, juce::int64 startSample, juce::int64 endSample) const;

    int getNumChannels() const noexcept {
        return numChannels_;
    }
    juce::int64 getNumSourceSamples() const noexcept {
        return sourceLengthSamples_;
    }

  private:
    WaveformPeakCache() = default;

    static juce::File getCacheFileFor(const juce::File& sourceFile);
    static juce::File getCacheRoot();

    int numChannels_ = 0;
    double sampleRate_ = 0.0;
    juce::int64 sourceLengthSamples_ = 0;
    juce::int64 numBuckets_ = 0;

    // Per-channel packed pairs: peaks_[ch][i*2+0]=min, peaks_[ch][i*2+1]=max
    // (int16, normalized by 32767).
    std::vector<std::vector<std::int16_t>> peaks_;

    JUCE_DECLARE_NON_COPYABLE(WaveformPeakCache)
};

}  // namespace magda
