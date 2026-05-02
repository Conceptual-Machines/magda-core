#include "WaveformPeakCache.hpp"

#include <algorithm>
#include <cstring>

namespace magda {

namespace {

constexpr juce::uint32 kMagic = 0x4B50474D;  // 'MGPK' little-endian
constexpr juce::uint32 kVersion = 1;

// Header layout — see class comment for the field meanings. Kept as POD so the
// stream read/write is a single block per direction.
#pragma pack(push, 1)
struct PeakFileHeader {
    juce::uint32 magic;
    juce::uint32 version;
    juce::uint32 samplesPerPeak;
    juce::uint16 numChannels;
    juce::uint16 reserved;
    double sampleRate;
    juce::int64 sourceLengthSamples;
    juce::int64 sourceFileSize;
    juce::int64 sourceFileModTimeMillis;
    juce::uint64 numBucketsPerChannel;
};
#pragma pack(pop)
static_assert(sizeof(PeakFileHeader) == 56, "PeakFileHeader must be tightly packed");

inline std::int16_t floatToInt16Peak(float v) noexcept {
    // Clamp before scaling — JUCE readers can deliver values fractionally
    // outside [-1,1] for int formats with full-scale samples.
    v = juce::jlimit(-1.0f, 1.0f, v);
    return static_cast<std::int16_t>(v * 32767.0f);
}

inline float int16PeakToFloat(std::int16_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 32767.0f);
}

}  // namespace

juce::File WaveformPeakCache::getCacheRoot() {
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("magda")
                    .getChildFile("peaks");
    if (!root.isDirectory())
        root.createDirectory();
    return root;
}

juce::File WaveformPeakCache::getCacheFileFor(const juce::File& sourceFile) {
    // hashCode64 is path-derived. The header re-validates against size+mtime,
    // so two files that happen to share a hash still won't pollute each other.
    return getCacheRoot().getChildFile(juce::String::toHexString(sourceFile.hashCode64()) + ".mpk");
}

std::unique_ptr<WaveformPeakCache> WaveformPeakCache::loadFromDisk(const juce::File& sourceFile) {
    if (!sourceFile.existsAsFile())
        return nullptr;

    const auto cacheFile = getCacheFileFor(sourceFile);
    if (!cacheFile.existsAsFile())
        return nullptr;

    juce::FileInputStream in(cacheFile);
    if (!in.openedOk())
        return nullptr;

    PeakFileHeader header{};
    if (in.read(&header, sizeof(header)) != static_cast<int>(sizeof(header)))
        return nullptr;

    if (header.magic != kMagic || header.version != kVersion ||
        header.samplesPerPeak != static_cast<juce::uint32>(SAMPLES_PER_PEAK) ||
        header.numChannels == 0 || header.numChannels > 64)
        return nullptr;

    // Invalidate on any change to the source file's identity.
    if (header.sourceFileSize != sourceFile.getSize() ||
        header.sourceFileModTimeMillis != sourceFile.getLastModificationTime().toMilliseconds())
        return nullptr;

    std::unique_ptr<WaveformPeakCache> cache(new WaveformPeakCache());
    cache->numChannels_ = header.numChannels;
    cache->sampleRate_ = header.sampleRate;
    cache->sourceLengthSamples_ = header.sourceLengthSamples;
    cache->numBuckets_ = static_cast<juce::int64>(header.numBucketsPerChannel);
    cache->peaks_.resize(static_cast<size_t>(cache->numChannels_));

    const size_t bytesPerChannel =
        static_cast<size_t>(cache->numBuckets_) * 2 * sizeof(std::int16_t);
    for (auto& chPeaks : cache->peaks_) {
        chPeaks.resize(static_cast<size_t>(cache->numBuckets_) * 2);
        if (bytesPerChannel > 0) {
            const int got = in.read(chPeaks.data(), static_cast<int>(bytesPerChannel));
            if (got != static_cast<int>(bytesPerChannel))
                return nullptr;
        }
    }

    return cache;
}

std::unique_ptr<WaveformPeakCache> WaveformPeakCache::computeAndWrite(
    const juce::File& sourceFile, juce::AudioFormatReader& reader) {
    const int numChannels = static_cast<int>(reader.numChannels);
    const juce::int64 totalSamples = reader.lengthInSamples;

    if (numChannels <= 0 || numChannels > 64 || totalSamples <= 0 || reader.sampleRate <= 0.0)
        return nullptr;

    const juce::int64 numBuckets = (totalSamples + SAMPLES_PER_PEAK - 1) / SAMPLES_PER_PEAK;

    std::unique_ptr<WaveformPeakCache> cache(new WaveformPeakCache());
    cache->numChannels_ = numChannels;
    cache->sampleRate_ = reader.sampleRate;
    cache->sourceLengthSamples_ = totalSamples;
    cache->numBuckets_ = numBuckets;
    cache->peaks_.assign(static_cast<size_t>(numChannels),
                         std::vector<std::int16_t>(static_cast<size_t>(numBuckets) * 2, 0));

    // Chunked walk — sized so each chunk is many full buckets, keeping the
    // outer per-bucket arithmetic cheap and the read syscalls infrequent.
    constexpr int kBucketsPerChunk = 1024;
    constexpr int kChunkSamples = kBucketsPerChunk * SAMPLES_PER_PEAK;

    juce::AudioBuffer<float> chunkBuffer(numChannels, kChunkSamples);

    juce::int64 sampleCursor = 0;
    juce::int64 bucketIdx = 0;

    while (sampleCursor < totalSamples) {
        const juce::int64 remaining = totalSamples - sampleCursor;
        const int toRead = static_cast<int>(std::min<juce::int64>(remaining, kChunkSamples));
        chunkBuffer.clear(0, toRead);
        if (!reader.read(&chunkBuffer, 0, toRead, sampleCursor, true, true))
            return nullptr;

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* samples = chunkBuffer.getReadPointer(ch);
            auto& chPeaks = cache->peaks_[static_cast<size_t>(ch)];

            int s = 0;
            juce::int64 b = bucketIdx;
            while (s < toRead) {
                const int bucketEnd = std::min(s + SAMPLES_PER_PEAK, toRead);
                float minVal = 1.0f;
                float maxVal = -1.0f;
                for (int k = s; k < bucketEnd; ++k) {
                    const float v = samples[k];
                    if (v < minVal)
                        minVal = v;
                    if (v > maxVal)
                        maxVal = v;
                }
                // Empty bucket (only at EOF) collapses to silence.
                if (minVal > maxVal) {
                    minVal = 0.0f;
                    maxVal = 0.0f;
                }
                chPeaks[static_cast<size_t>(b * 2)] = floatToInt16Peak(minVal);
                chPeaks[static_cast<size_t>(b * 2 + 1)] = floatToInt16Peak(maxVal);
                ++b;
                s = bucketEnd;
            }
        }

        sampleCursor += toRead;
        bucketIdx += (toRead + SAMPLES_PER_PEAK - 1) / SAMPLES_PER_PEAK;
    }

    // Write to disk. Use a temp file + rename so a crashed write never leaves
    // a half-written .mpk in place that would deserialize successfully.
    const auto cacheFile = getCacheFileFor(sourceFile);
    cacheFile.getParentDirectory().createDirectory();
    const auto tempFile = cacheFile.getSiblingFile(cacheFile.getFileName() + ".tmp");
    tempFile.deleteFile();

    {
        juce::FileOutputStream out(tempFile);
        if (!out.openedOk())
            return nullptr;

        PeakFileHeader header{};
        header.magic = kMagic;
        header.version = kVersion;
        header.samplesPerPeak = static_cast<juce::uint32>(SAMPLES_PER_PEAK);
        header.numChannels = static_cast<juce::uint16>(numChannels);
        header.reserved = 0;
        header.sampleRate = reader.sampleRate;
        header.sourceLengthSamples = totalSamples;
        header.sourceFileSize = sourceFile.getSize();
        header.sourceFileModTimeMillis = sourceFile.getLastModificationTime().toMilliseconds();
        header.numBucketsPerChannel = static_cast<juce::uint64>(numBuckets);

        if (!out.write(&header, sizeof(header)))
            return nullptr;

        for (const auto& chPeaks : cache->peaks_) {
            const size_t bytes = chPeaks.size() * sizeof(std::int16_t);
            if (bytes > 0 && !out.write(chPeaks.data(), bytes))
                return nullptr;
        }

        out.flush();
    }

    if (!tempFile.moveFileTo(cacheFile)) {
        tempFile.deleteFile();
        return nullptr;
    }

    return cache;
}

WaveformPeakCache::MinMax WaveformPeakCache::getMinMaxForRange(int channel, juce::int64 startSample,
                                                               juce::int64 endSample) const {
    MinMax result;

    if (channel < 0 || channel >= numChannels_)
        return result;
    if (numBuckets_ <= 0)
        return result;

    endSample = juce::jlimit<juce::int64>(0, sourceLengthSamples_, endSample);
    startSample = juce::jlimit<juce::int64>(0, endSample, startSample);
    if (startSample >= endSample)
        return result;

    const juce::int64 firstBucket = startSample / SAMPLES_PER_PEAK;
    // -1 to make the range half-open in bucket space.
    const juce::int64 lastBucket = std::min(numBuckets_ - 1, (endSample - 1) / SAMPLES_PER_PEAK);

    const auto& chPeaks = peaks_[static_cast<size_t>(channel)];

    std::int16_t minI = std::numeric_limits<std::int16_t>::max();
    std::int16_t maxI = std::numeric_limits<std::int16_t>::min();

    for (juce::int64 b = firstBucket; b <= lastBucket; ++b) {
        const auto bMin = chPeaks[static_cast<size_t>(b * 2)];
        const auto bMax = chPeaks[static_cast<size_t>(b * 2 + 1)];
        if (bMin < minI)
            minI = bMin;
        if (bMax > maxI)
            maxI = bMax;
    }

    if (minI > maxI) {
        result.min = 0.0f;
        result.max = 0.0f;
    } else {
        result.min = int16PeakToFloat(minI);
        result.max = int16PeakToFloat(maxI);
    }
    return result;
}

}  // namespace magda
