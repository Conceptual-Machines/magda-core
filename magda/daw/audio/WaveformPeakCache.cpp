#include "WaveformPeakCache.hpp"

#include <algorithm>

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
