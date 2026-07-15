#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "insert_capture/CaptureWindowMath.hpp"

// External-insert export capture (#1623): the capture-window sample mapping
// shared by the hidden InsertCapturePlugin's record and playback modes.

using namespace magda;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
constexpr double kBlockSec = kBlock / kSr;
}  // namespace

TEST_CASE("Capture window mapping", "[insert-freeze][capture-window]") {
    const double winStart = 1.0;
    const double winEnd = 3.0;

    SECTION("block entirely before the window writes nothing") {
        auto m = mapBlockToCaptureWindow(0.0, kBlockSec, kBlock, winStart, winEnd, kSr);
        REQUIRE(m.numSamples == 0);
        REQUIRE_FALSE(m.windowFinished);
    }

    SECTION("block straddling the window start is trimmed to the overlap") {
        const double blockStart = winStart - kBlockSec / 2.0;
        auto m = mapBlockToCaptureWindow(blockStart, blockStart + kBlockSec, kBlock, winStart,
                                         winEnd, kSr);
        REQUIRE(m.bufferOffset == kBlock / 2);
        REQUIRE(m.numSamples == kBlock / 2);
        REQUIRE(m.fileStartSample == 0);
    }

    SECTION("consecutive blocks map to contiguous file positions") {
        juce::int64 filePos = 0;
        // Start away from an exact block boundary to exercise rounding.
        double t = winStart - 0.0031;
        bool started = false;
        while (t < winStart + 0.5) {
            auto m = mapBlockToCaptureWindow(t, t + kBlockSec, kBlock, winStart, winEnd, kSr);
            if (m.numSamples > 0) {
                if (started)
                    REQUIRE(m.fileStartSample == filePos);
                else
                    started = true;
                filePos = m.fileStartSample + m.numSamples;
            }
            t += kBlockSec;
        }
        REQUIRE(started);
    }

    SECTION("block straddling the window end is trimmed") {
        const double blockStart = winEnd - kBlockSec / 2.0;
        auto m = mapBlockToCaptureWindow(blockStart, blockStart + kBlockSec, kBlock, winStart,
                                         winEnd, kSr);
        REQUIRE(m.numSamples == kBlock / 2);
        REQUIRE(m.bufferOffset == 0);
        REQUIRE_FALSE(m.windowFinished);
    }

    SECTION("block at/after the window end reports finished") {
        auto m = mapBlockToCaptureWindow(winEnd, winEnd + kBlockSec, kBlock, winStart, winEnd, kSr);
        REQUIRE(m.windowFinished);
        REQUIRE(m.numSamples == 0);
    }

    SECTION("degenerate inputs write nothing") {
        REQUIRE(mapBlockToCaptureWindow(0.0, kBlockSec, 0, winStart, winEnd, kSr).numSamples == 0);
        REQUIRE(mapBlockToCaptureWindow(0.0, kBlockSec, kBlock, winStart, winEnd, 0.0).numSamples ==
                0);
        REQUIRE(mapBlockToCaptureWindow(0.0, kBlockSec, kBlock, winEnd, winStart, kSr).numSamples ==
                0);
    }
}
