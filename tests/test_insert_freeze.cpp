#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/insert_freeze/CaptureWindowMath.hpp"
#include "core/ExternalInsertFreeze.hpp"
#include "project/serialization/ProjectSerializer.hpp"

// External-insert freeze (#1623): capture-window sample mapping used by the
// hidden InsertCapturePlugin on the audio thread, and the freeze-state
// round-trip through DeviceInfo serialization.

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

TEST_CASE("External-insert freeze state round-trips through DeviceInfo serialization",
          "[insert-freeze][serialization]") {
    DeviceInfo device;
    device.id = 7;
    device.name = "External Instrument";
    device.pluginId = "insert";
    device.isInstrument = true;

    auto freeze = std::make_shared<ExternalInsertFreeze>();
    freeze->captureFile = "Freeze/synth_20260708.wav";
    freeze->frozenClipId = 42;
    freeze->bypassedDevices = {3, 7};

    ClipInfo midiClip;  // ClipInfo defaults to MIDI content
    midiClip.id = 11;
    midiClip.trackId = 2;
    midiClip.name = "Synth line";
    midiClip.setPlacementBeats(4.0, 8.0);
    freeze->stashedClips.push_back(midiClip);

    device.externalFreeze = freeze;

    auto json = ProjectSerializer::serializeDeviceInfo(device);

    DeviceInfo restored;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, restored));
    REQUIRE(restored.isFrozen());
    REQUIRE(restored.externalFreeze->captureFile == "Freeze/synth_20260708.wav");
    REQUIRE(restored.externalFreeze->frozenClipId == 42);
    REQUIRE(restored.externalFreeze->bypassedDevices == std::vector<DeviceId>{3, 7});

    REQUIRE(restored.externalFreeze->stashedClips.size() == 1);
    const auto& clip = restored.externalFreeze->stashedClips.front();
    REQUIRE(clip.id == 11);
    REQUIRE(clip.trackId == 2);
    REQUIRE(clip.isMidi());
    REQUIRE(clip.getStartBeats(120.0) == Approx(4.0));
    REQUIRE(clip.getEndBeats(120.0) == Approx(12.0));
}

TEST_CASE("DeviceInfo without freeze state stays unfrozen after a round-trip",
          "[insert-freeze][serialization]") {
    DeviceInfo device;
    device.id = 1;
    device.name = "External FX";
    device.pluginId = "insert";

    auto json = ProjectSerializer::serializeDeviceInfo(device);

    DeviceInfo restored;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, restored));
    REQUIRE_FALSE(restored.isFrozen());
}
