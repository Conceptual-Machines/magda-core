// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaveformPeakCache.hpp"

#include <algorithm>
#include <limits>

namespace magda {

namespace {

constexpr juce::uint32 kMagic = 0x4B50474D;  // 'MGPK' little-endian
constexpr juce::uint32 kVersion = 1;

// Header layout, see class comment for the field meanings. Kept as POD so the
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

}  // namespace

WaveformPeakCache::WaveformPeakCache(sdk::PeakData peaks) : peaks_(std::move(peaks)) {
    const auto numChannels = static_cast<std::size_t>(peaks_.numChannels());
    std::int64_t fineBuckets = peaks_.numBuckets();
    while (fineBuckets >= COARSE_FACTOR * COARSE_FACTOR) {
        const std::int64_t coarseBuckets = (fineBuckets + COARSE_FACTOR - 1) / COARSE_FACTOR;
        std::vector<std::vector<std::int16_t>> level(numChannels);
        for (std::size_t ch = 0; ch < numChannels; ++ch) {
            const std::int16_t* fine = coarse_.empty()
                                           ? peaks_.channelPeaks(static_cast<int>(ch)).data()
                                           : coarse_.back()[ch].data();
            auto& out = level[ch];
            out.resize(static_cast<std::size_t>(coarseBuckets) * 2);
            for (std::int64_t b = 0; b < coarseBuckets; ++b) {
                const std::int64_t end = std::min((b + 1) * COARSE_FACTOR, fineBuckets);
                auto minI = std::numeric_limits<std::int16_t>::max();
                auto maxI = std::numeric_limits<std::int16_t>::min();
                for (std::int64_t f = b * COARSE_FACTOR; f < end; ++f) {
                    minI = std::min(minI, fine[f * 2]);
                    maxI = std::max(maxI, fine[f * 2 + 1]);
                }
                out[static_cast<std::size_t>(b) * 2] = minI;
                out[static_cast<std::size_t>(b) * 2 + 1] = maxI;
            }
        }
        coarse_.push_back(std::move(level));
        fineBuckets = coarseBuckets;
    }
}

WaveformPeakCache::MinMax WaveformPeakCache::getMinMaxForRange(int channel,
                                                               std::int64_t startSample,
                                                               std::int64_t endSample) const {
    const std::int64_t numBuckets = peaks_.numBuckets();
    if (channel < 0 || channel >= peaks_.numChannels() || numBuckets <= 0)
        return {};

    endSample = std::clamp<std::int64_t>(endSample, 0, peaks_.numSourceSamples());
    startSample = std::clamp<std::int64_t>(startSample, 0, endSample);
    if (startSample >= endSample)
        return {};

    auto minI = std::numeric_limits<std::int16_t>::max();
    auto maxI = std::numeric_limits<std::int16_t>::min();
    const auto scan = [&](const std::int16_t* buckets, std::int64_t from, std::int64_t to) {
        for (std::int64_t b = from; b < to; ++b) {
            minI = std::min(minI, buckets[b * 2]);
            maxI = std::max(maxI, buckets[b * 2 + 1]);
        }
    };

    // Half-open bucket range, narrowed at each level to the part whole coarser buckets cover.
    std::int64_t first = startSample / SAMPLES_PER_PEAK;
    std::int64_t last = std::min(numBuckets, (endSample - 1) / SAMPLES_PER_PEAK + 1);
    const std::int16_t* buckets = peaks_.channelPeaks(channel).data();
    for (const auto& coarser : coarse_) {
        if (last - first < 2 * COARSE_FACTOR)
            break;
        const std::int64_t alignedFirst = (first + COARSE_FACTOR - 1) / COARSE_FACTOR;
        const std::int64_t alignedLast = last / COARSE_FACTOR;
        scan(buckets, first, alignedFirst * COARSE_FACTOR);
        scan(buckets, alignedLast * COARSE_FACTOR, last);
        first = alignedFirst;
        last = alignedLast;
        buckets = coarser[static_cast<std::size_t>(channel)].data();
    }
    scan(buckets, first, last);

    if (minI > maxI)
        return {};
    return {sdk::peakToFloat(minI), sdk::peakToFloat(maxI)};
}

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

    // Reject a header whose bucket count disagrees with its length before sizing buffers from it.
    const auto expectedBuckets =
        (header.sourceLengthSamples + SAMPLES_PER_PEAK - 1) / SAMPLES_PER_PEAK;
    if (header.sourceLengthSamples < 0 ||
        header.numBucketsPerChannel != static_cast<juce::uint64>(expectedBuckets))
        return nullptr;

    const size_t valuesPerChannel = static_cast<size_t>(header.numBucketsPerChannel) * 2;
    std::vector<std::vector<std::int16_t>> channels(header.numChannels);
    for (auto& chPeaks : channels) {
        chPeaks.resize(valuesPerChannel);
        const size_t bytes = valuesPerChannel * sizeof(std::int16_t);
        if (bytes > 0 &&
            in.read(chPeaks.data(), static_cast<int>(bytes)) != static_cast<int>(bytes))
            return nullptr;
    }

    auto peaks = sdk::PeakData::fromPacked(header.sourceLengthSamples, std::move(channels));
    if (!peaks)
        return nullptr;
    return std::unique_ptr<WaveformPeakCache>(new WaveformPeakCache(std::move(*peaks)));
}

std::unique_ptr<WaveformPeakCache> WaveformPeakCache::computeAndWrite(
    const juce::File& sourceFile, juce::AudioFormatReader& reader) {
    const int numChannels = static_cast<int>(reader.numChannels);
    const juce::int64 totalSamples = reader.lengthInSamples;

    if (numChannels <= 0 || numChannels > 64 || totalSamples <= 0 || reader.sampleRate <= 0.0)
        return nullptr;

    sdk::PeakData peaks(numChannels, totalSamples);

    // Each chunk is many whole buckets, so only the last block ends mid-bucket.
    constexpr int kBucketsPerChunk = 1024;
    constexpr int kChunkSamples = kBucketsPerChunk * SAMPLES_PER_PEAK;

    juce::AudioBuffer<float> chunkBuffer(numChannels, kChunkSamples);

    for (juce::int64 cursor = 0; cursor < totalSamples; cursor += kChunkSamples) {
        const int toRead =
            static_cast<int>(std::min<juce::int64>(totalSamples - cursor, kChunkSamples));
        chunkBuffer.clear(0, toRead);
        if (!reader.read(&chunkBuffer, 0, toRead, cursor, true, true))
            return nullptr;
        if (!peaks.addBlock(
                ConstBufferView(chunkBuffer.getArrayOfReadPointers(), numChannels, toRead), cursor))
            return nullptr;
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
        header.numBucketsPerChannel = static_cast<juce::uint64>(peaks.numBuckets());

        if (!out.write(&header, sizeof(header)))
            return nullptr;

        for (int ch = 0; ch < numChannels; ++ch) {
            const auto chPeaks = peaks.channelPeaks(ch);
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

    return std::unique_ptr<WaveformPeakCache>(new WaveformPeakCache(std::move(peaks)));
}

}  // namespace magda
