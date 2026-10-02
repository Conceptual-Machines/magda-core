#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

#include "magda/daw/audio/WaveformPeakCache.hpp"

namespace {

constexpr std::size_t kHeaderBytes = 56;

struct Fixture {
    const char* name;
    int channels;
    int length;
    bool floatWav;
    std::uint64_t payloadHash;
};

/// Noise whose amplitude steps down every bucket, with the 16-bit extremes pinned at both ends.
std::vector<std::int16_t> fixtureChannel(int channel, int length) {
    std::vector<std::int16_t> out(static_cast<std::size_t>(length));
    std::uint32_t s = 12345u + static_cast<std::uint32_t>(channel) * 7919u;
    for (int i = 0; i < length; ++i) {
        s = s * 1664525u + 1013904223u;
        out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(s >> 16)) >> ((i / 64) % 8));
    }
    out.back() = 32767;
    if (channel == 0)
        out.front() = -32768;
    return out;
}

/// Writes a WAV by hand: JUCE's writer scales by 2^31-1, which would shift the 16-bit samples.
bool writeWav(const juce::File& file, const std::vector<std::vector<std::int16_t>>& channels,
              bool floatSamples) {
    const auto numChannels = static_cast<std::uint32_t>(channels.size());
    const auto frames = static_cast<std::uint32_t>(channels[0].size());
    const std::uint32_t bytesPerSample = floatSamples ? 4 : 2;
    const std::uint32_t dataBytes = frames * numChannels * bytesPerSample;

    juce::MemoryOutputStream out;
    auto u32 = [&](std::uint32_t v) { out.writeInt(static_cast<int>(v)); };
    auto u16 = [&](std::uint16_t v) { out.writeShort(static_cast<short>(v)); };
    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(floatSamples ? 3 : 1);
    u16(static_cast<std::uint16_t>(numChannels));
    u32(44100);
    u32(44100 * numChannels * bytesPerSample);
    u16(static_cast<std::uint16_t>(numChannels * bytesPerSample));
    u16(static_cast<std::uint16_t>(bytesPerSample * 8));
    out.write("data", 4);
    u32(dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
        for (const auto& channel : channels) {
            if (floatSamples)
                out.writeFloat(static_cast<float>(channel[i]) / 16384.0f);
            else
                u16(static_cast<std::uint16_t>(channel[i]));
        }
    return file.replaceWithData(out.getData(), out.getDataSize());
}

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

TEST_CASE("WaveformPeakCache - cache file bytes are pinned", "[audio][peaks]") {
    // Hashes captured from the pre-SDK implementation; a change here changes every user's cache.
    static const Fixture kFixtures[] = {
        {"mono_1", 1, 1, false, 4368492281661021131ull},
        {"mono_63", 1, 63, false, 4453476833923492929ull},
        {"mono_64", 1, 64, false, 4453476833923492929ull},
        {"mono_65", 1, 65, false, 9301015227453570985ull},
        {"mono_1000", 1, 1000, false, 9983402623296114481ull},
        {"stereo_4097", 2, 4097, false, 429778391380076314ull},
        {"stereo_65536", 2, 65536, false, 1080836212127174483ull},
        {"stereo_65537", 2, 65537, false, 8666968538843584175ull},
        {"stereo_131201", 2, 131201, false, 3670888310760834335ull},
        {"tri_5000", 3, 5000, false, 1718059950880015234ull},
        {"float_mono_100003", 1, 100003, true, 4144862335892596701ull},
        {"float_stereo_70001", 2, 70001, true, 12213278077523292931ull},
    };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& f : kFixtures) {
        INFO(f.name);
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("magda-peak-cache-test-" + juce::Uuid().toString());
        dir.createDirectory();
        const auto file = dir.getChildFile(juce::String(f.name) + ".wav");

        {
            std::vector<std::vector<std::int16_t>> channels;
            for (int ch = 0; ch < f.channels; ++ch)
                channels.push_back(fixtureChannel(ch, f.length));
            REQUIRE(writeWav(file, channels, f.floatWav));
        }

        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        REQUIRE(reader != nullptr);
        const auto cache = magda::WaveformPeakCache::computeAndWrite(file, *reader);
        REQUIRE(cache != nullptr);

        const auto cacheFile = magda::WaveformPeakCache::getCacheFileFor(file);
        juce::MemoryBlock bytes;
        REQUIRE(cacheFile.loadFileAsData(bytes));

        const auto loaded = magda::WaveformPeakCache::loadFromDisk(file);
        REQUIRE(loaded != nullptr);
        CHECK(loaded->getNumChannels() == f.channels);
        CHECK(loaded->getNumSourceSamples() == f.length);
        for (int ch = 0; ch < f.channels; ++ch) {
            const auto written = cache->getMinMaxForRange(ch, 0, f.length);
            const auto read = loaded->getMinMaxForRange(ch, 0, f.length);
            CHECK(read.min == written.min);
            CHECK(read.max == written.max);
        }

        cacheFile.deleteFile();
        dir.deleteRecursively();

        const auto* data = static_cast<const std::uint8_t*>(bytes.getData());
        const std::size_t numBuckets = (static_cast<std::size_t>(f.length) + 63) / 64;
        REQUIRE(bytes.getSize() ==
                kHeaderBytes + static_cast<std::size_t>(f.channels) * numBuckets * 4);

        const auto hash = fnv1a(data + kHeaderBytes, bytes.getSize() - kHeaderBytes);
        INFO("hash " << hash);
        CHECK(hash == f.payloadHash);
    }
}
