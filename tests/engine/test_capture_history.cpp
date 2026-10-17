#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "io/CaptureHistory.hpp"
#include "io/CaptureRing.hpp"

/// @file The capture ring with a rolling file behind it (#3052).

using magda::engine::CaptureHistory;
using magda::engine::CaptureRing;

namespace {

constexpr int kBlock = 1024;
constexpr double kRate = 1024.0;               // so a block is a second
constexpr std::int64_t kRingFrames = 1 << 15;  // the smallest ring the write margin allows

/// Block @p index's level, so a frame read back says which block it came from.
float levelOf(int index) {
    return 0.01f * static_cast<float>(1 + index % 90);
}

void writeBlock(CaptureRing& ring, int index, bool playing) {
    juce::AudioBuffer<float> block(2, kBlock);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), levelOf(index), kBlock);
    ring.write(block, {static_cast<double>(index), index + 1.0}, playing);
}

void drainAll(CaptureHistory& history) {
    while (history.drain()) {
    }
}

/// Blocks [@p from, @p to), drained every eight so the file keeps up with the ring.
void record(CaptureRing& ring, CaptureHistory& history, int from, int to, bool playing = true) {
    for (int index = from; index < to; ++index) {
        writeBlock(ring, index, playing);
        if (index % 8 == 7)
            drainAll(history);
    }
    drainAll(history);
}

struct OnDisk {
    explicit OnDisk(std::int64_t diskFrames)
        : file(juce::File::createTempFile(".raw")),
          history(ring, file, std::make_unique<juce::FileOutputStream>(file), diskFrames) {}

    CaptureRing ring{2, kRingFrames, kRate};
    juce::File file;
    CaptureHistory history;
};

}  // namespace

TEST_CASE("Without a file the history is the ring", "[engine][capture][3052]") {
    CaptureRing ring(2, kRingFrames, kRate);
    CaptureHistory history(ring);
    for (int index = 0; index < 8; ++index)
        writeBlock(ring, index, true);

    CHECK_FALSE(history.onDisk());
    CHECK_FALSE(history.drain());
    CHECK(history.capacityFrames() == ring.capacityFrames());
    CHECK(history.meter(4).oldestReadable == ring.meter(4).oldestReadable);
}

TEST_CASE("Audio that has left the ring reads back from the file", "[engine][capture][3052]") {
    OnDisk disk(1 << 18);
    record(disk.ring, disk.history, 0, 128);

    // The ring holds the last 32 blocks at most; the file holds them all.
    REQUIRE(disk.ring.oldestReadable() > 64 * kBlock);
    const auto meter = disk.history.meter(8);
    CHECK(meter.oldestReadable == 0);
    CHECK(meter.capacityFrames == 1 << 18);

    SECTION("wholly from the file") {
        juce::AudioBuffer<float> out;
        REQUIRE(disk.history.read(10 * kBlock, 2 * kBlock, out));
        CHECK(out.getSample(0, 0) == levelOf(10));
        CHECK(out.getSample(1, kBlock + 5) == levelOf(11));
    }

    SECTION("across the file and the ring") {
        const auto first = disk.ring.oldestReadable() - kBlock;
        juce::AudioBuffer<float> out;
        REQUIRE(disk.history.read(first, 4 * kBlock, out));
        for (int frame = 0; frame < out.getNumSamples(); frame += 256)
            CHECK(out.getSample(0, frame) == levelOf(static_cast<int>((first + frame) / kBlock)));
    }

    SECTION("peaks from the file's chunks and from its samples") {
        const auto coarse = disk.history.peaks(0, 8 * kBlock, 8);
        CHECK(coarse[3] == levelOf(3));
        const auto fine = disk.history.peaks(5 * kBlock, 5 * kBlock + 64, 4);
        CHECK(fine[2] == levelOf(5));
    }
}

TEST_CASE("A pass that began before the ring is kept from its start", "[engine][capture][3052]") {
    OnDisk disk(1 << 18);
    record(disk.ring, disk.history, 0, 4, false);
    record(disk.ring, disk.history, 4, 100, true);

    const auto meter = disk.history.meter(1);
    CHECK(meter.pass.start == 4 * kBlock);
    CHECK(meter.pass.end == 100 * kBlock);
    CHECK(disk.ring.meter(1).pass.start > 4 * kBlock);
}

TEST_CASE("A file that fell behind the ring starts again after the gap",
          "[engine][capture][3052]") {
    OnDisk disk(1 << 18);
    record(disk.ring, disk.history, 0, 16);
    // Twice the ring with nothing drained: the first of it was overwritten unseen.
    for (int index = 16; index < 80; ++index)
        writeBlock(disk.ring, index, true);
    drainAll(disk.history);

    juce::AudioBuffer<float> out;
    CHECK_FALSE(disk.history.read(0, kBlock, out));
    CHECK(disk.history.meter(1).oldestReadable == disk.ring.oldestReadable());

    record(disk.ring, disk.history, 80, 160);
    CHECK(disk.history.meter(1).oldestReadable < disk.ring.oldestReadable());
    REQUIRE(disk.history.read(100 * kBlock, kBlock, out));
    CHECK(out.getSample(0, 0) == levelOf(100));
}

TEST_CASE("The file laps like the ring does", "[engine][capture][3052]") {
    OnDisk disk(1 << 17);  // 128 blocks
    record(disk.ring, disk.history, 0, 400);

    const auto oldest = disk.history.meter(1).oldestReadable;
    CHECK(oldest > 400 * kBlock - (1 << 17));
    juce::AudioBuffer<float> out;
    CHECK_FALSE(disk.history.read(oldest - kBlock, kBlock, out));
    REQUIRE(disk.history.read(oldest, kBlock, out));
    CHECK(out.getSample(0, 0) == levelOf(static_cast<int>(oldest / kBlock)));
}

TEST_CASE("The file goes with the history", "[engine][capture][3052]") {
    juce::File file;
    {
        OnDisk disk(1 << 17);
        file = disk.file;
        record(disk.ring, disk.history, 0, 8);
        CHECK(file.getSize() > 0);
    }
    CHECK_FALSE(file.exists());
}
