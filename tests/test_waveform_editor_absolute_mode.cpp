#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"

/**
 * Tests for waveform editor source-relative positioning logic
 *
 * The waveform editor is a view into the source file. Arrangement placement
 * must not move the displayed waveform.
 *
 * These tests verify the coordinate conversion logic the editor relies on.
 * The actual UI component (WaveformGridComponent) uses this same math.
 */

namespace {

// Constants matching WaveformGridComponent
constexpr int LEFT_PADDING = 10;

/**
 * Replicate the coordinate conversion logic from WaveformGridComponent
 * This allows testing without JUCE UI dependencies
 */
class WaveformCoordinateConverter {
  public:
    void setRelativeMode(bool relative) {
        relativeMode_ = relative;
    }
    void setHorizontalZoom(double pixelsPerSecond) {
        horizontalZoom_ = pixelsPerSecond;
    }
    void updateClipPosition(double startTime, double length) {
        clipStartTime_ = startTime;
        clipLength_ = length;
    }
    void updateSourcePositions(double sampleStart, double offset) {
        sampleStart_ = sampleStart;
        offset_ = offset;
    }

    double getClipStartTime() const {
        return clipStartTime_;
    }
    double getClipLength() const {
        return clipLength_;
    }

    int timeToPixel(double time) const {
        return static_cast<int>(time * horizontalZoom_) + LEFT_PADDING;
    }

    double pixelToTime(int x) const {
        return (x - LEFT_PADDING) / horizontalZoom_;
    }
    double getDisplayStartTime() const {
        return relativeMode_ ? 0.0 : clipStartTime_ - sampleStart_;
    }
    int sampleStartPixel() const {
        return timeToPixel(getDisplayStartTime() + sampleStart_);
    }
    int offsetPixel() const {
        return timeToPixel(getDisplayStartTime() + offset_);
    }

  private:
    bool relativeMode_ = false;
    double clipStartTime_ = 0.0;
    double clipLength_ = 0.0;
    double sampleStart_ = 0.0;
    double offset_ = 0.0;
    double horizontalZoom_ = 100.0;
};

}  // namespace

using namespace magda;

TEST_CASE("WaveformCoordinateConverter - updateClipPosition updates internal state",
          "[waveform][grid][relative]") {
    WaveformCoordinateConverter converter;

    SECTION("Initial state has zero clip position") {
        converter.setRelativeMode(true);
        converter.setHorizontalZoom(100.0);

        REQUIRE(converter.getClipStartTime() == 0.0);
        REQUIRE(converter.getClipLength() == 0.0);

        int pixelAt0 = converter.timeToPixel(0.0);
        REQUIRE(pixelAt0 == LEFT_PADDING);
    }

    SECTION("updateClipPosition updates start time") {
        converter.setRelativeMode(true);
        converter.setHorizontalZoom(100.0);

        // Clip at bar 1 (0 seconds)
        converter.updateClipPosition(0.0, 4.0);
        REQUIRE(converter.getClipStartTime() == 0.0);
        REQUIRE(converter.getClipLength() == 4.0);

        // Move clip to bar 2 (2 seconds at 120 BPM)
        converter.updateClipPosition(2.0, 4.0);
        REQUIRE(converter.getClipStartTime() == 2.0);
        REQUIRE(converter.getClipLength() == 4.0);

        // Verify coordinate conversion
        int pixelAtClipStart = converter.timeToPixel(2.0);
        REQUIRE(pixelAtClipStart == 210);  // 2.0 * 100 + 10

        int pixelAtClipEnd = converter.timeToPixel(6.0);
        REQUIRE(pixelAtClipEnd == 610);  // 6.0 * 100 + 10
    }

    SECTION("updateClipPosition updates length") {
        converter.setRelativeMode(true);
        converter.setHorizontalZoom(100.0);

        converter.updateClipPosition(0.0, 4.0);
        REQUIRE(converter.getClipLength() == 4.0);

        converter.updateClipPosition(0.0, 8.0);
        REQUIRE(converter.getClipLength() == 8.0);

        int pixelAtClipEnd = converter.timeToPixel(8.0);
        REQUIRE(pixelAtClipEnd == 810);  // 8.0 * 100 + 10
    }
}

TEST_CASE("WaveformCoordinateConverter - Coordinate conversion", "[waveform][grid][coordinates]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);
    converter.setHorizontalZoom(100.0);

    SECTION("timeToPixel returns correct pixel positions") {
        REQUIRE(converter.timeToPixel(0.0) == 10);   // 0 * 100 + 10
        REQUIRE(converter.timeToPixel(1.0) == 110);  // 1.0 * 100 + 10
        REQUIRE(converter.timeToPixel(2.5) == 260);  // 2.5 * 100 + 10
    }

    SECTION("pixelToTime returns correct time positions") {
        REQUIRE(converter.pixelToTime(10) == Catch::Approx(0.0));
        REQUIRE(converter.pixelToTime(110) == Catch::Approx(1.0));
        REQUIRE(converter.pixelToTime(260) == Catch::Approx(2.5));
    }

    SECTION("Round-trip conversion preserves values") {
        double originalTime = 3.7;
        int pixel = converter.timeToPixel(originalTime);
        double recoveredTime = converter.pixelToTime(pixel);

        REQUIRE(recoveredTime == Catch::Approx(originalTime).margin(0.01));
    }
}

TEST_CASE("WaveformCoordinateConverter - Different zoom levels", "[waveform][grid][zoom]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);

    SECTION("Zoom 50 pixels per second") {
        converter.setHorizontalZoom(50.0);
        REQUIRE(converter.timeToPixel(2.0) == 110);  // 2.0 * 50 + 10
    }

    SECTION("Zoom 200 pixels per second") {
        converter.setHorizontalZoom(200.0);
        REQUIRE(converter.timeToPixel(2.0) == 410);  // 2.0 * 200 + 10
    }

    SECTION("Changing zoom updates conversion") {
        converter.setHorizontalZoom(100.0);
        int pixelAt100 = converter.timeToPixel(1.0);

        converter.setHorizontalZoom(200.0);
        int pixelAt200 = converter.timeToPixel(1.0);

        REQUIRE(pixelAt100 == 110);
        REQUIRE(pixelAt200 == 210);
    }
}

TEST_CASE("WaveformCoordinateConverter - clip move does not move source-relative display",
          "[waveform][grid][relative][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);
    converter.setHorizontalZoom(100.0);
    converter.updateSourcePositions(0.0, 0.5);

    constexpr double BAR_DURATION = 2.0;  // At 120 BPM

    double clipStartBar1 = 0.0 * BAR_DURATION;
    double clipLength = 2.0 * BAR_DURATION;  // 2 bars = 4 seconds
    converter.updateClipPosition(clipStartBar1, clipLength);

    const int initialSampleStartX = converter.sampleStartPixel();
    const int initialOffsetX = converter.offsetPixel();

    REQUIRE(converter.getClipStartTime() == 0.0);
    REQUIRE(initialSampleStartX == 10);
    REQUIRE(initialOffsetX == 60);

    double clipStartBar2 = 1.0 * BAR_DURATION;  // Bar 2 = 2 seconds
    converter.updateClipPosition(clipStartBar2, clipLength);

    REQUIRE(converter.getClipStartTime() == 2.0);
    REQUIRE(converter.sampleStartPixel() == initialSampleStartX);
    REQUIRE(converter.offsetPixel() == initialOffsetX);
}

TEST_CASE("WaveformCoordinateConverter - multiple arrangement moves preserve source display",
          "[waveform][grid][relative][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);
    converter.setHorizontalZoom(100.0);
    converter.updateSourcePositions(0.0, 0.5);

    double clipLength = 4.0;  // 2 bars at 120 BPM
    const int expectedSampleStartX = converter.sampleStartPixel();
    const int expectedOffsetX = converter.offsetPixel();

    // Move clip through several positions
    std::vector<double> positions = {0.0, 2.0, 4.0, 8.0, 2.0, 0.0};

    for (double startTime : positions) {
        converter.updateClipPosition(startTime, clipLength);

        // Verify internal state
        REQUIRE(converter.getClipStartTime() == startTime);
        REQUIRE(converter.getClipLength() == clipLength);

        REQUIRE(converter.sampleStartPixel() == expectedSampleStartX);
        REQUIRE(converter.offsetPixel() == expectedOffsetX);
    }
}

TEST_CASE("WaveformCoordinateConverter - offset moves independently from sample start",
          "[waveform][grid][offset][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);
    converter.setHorizontalZoom(100.0);
    converter.updateClipPosition(4.0, 2.0);
    converter.updateSourcePositions(1.0, 1.0);

    const int initialSampleStartX = converter.sampleStartPixel();
    const int initialOffsetX = converter.offsetPixel();

    REQUIRE(initialSampleStartX == 110);
    REQUIRE(initialOffsetX == initialSampleStartX);

    converter.updateSourcePositions(1.0, 1.5);

    REQUIRE(converter.sampleStartPixel() == initialSampleStartX);
    REQUIRE(converter.offsetPixel() == 160);
    REQUIRE(converter.offsetPixel() != initialOffsetX);
}

TEST_CASE("WaveformCoordinateConverter - source-relative view ignores arrangement placement",
          "[waveform][grid][relative][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(true);
    converter.setHorizontalZoom(100.0);
    converter.updateSourcePositions(0.0, 0.5);

    converter.updateClipPosition(0.0, 4.0);
    const int initialSampleStartX = converter.sampleStartPixel();
    const int initialOffsetX = converter.offsetPixel();

    converter.updateClipPosition(8.0, 4.0);

    REQUIRE(converter.sampleStartPixel() == initialSampleStartX);
    REQUIRE(converter.offsetPixel() == initialOffsetX);
}

TEST_CASE("ClipOperations - audio sanitizing preserves sample start",
          "[clip][audio][offset][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_offset_regression.wav";
    clip.loopEnabled = false;
    clip.autoTempo = false;
    clip.length = 4.0;
    clip.speedRatio = 1.0;
    clip.loopStart = 0.25;
    clip.offset = 0.75;

    ClipOperations::sanitizeAudioToSourceDuration(clip, 8.0);

    REQUIRE(clip.offset == Catch::Approx(0.75));
    REQUIRE(clip.loopStart == Catch::Approx(0.25));
}

TEST_CASE("ClipOperations - audio sanitizing clamps length through beat placement",
          "[clip][audio][offset][beats][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_source_sanitize_beats.wav";
    clip.loopEnabled = false;
    clip.autoTempo = false;
    clip.startTime = 2.0;
    clip.length = 10.0;
    clip.setPlacementBeats(4.0, 20.0);
    clip.speedRatio = 1.0;
    clip.offset = 3.0;

    ClipOperations::sanitizeAudioToSourceDuration(clip, 8.0, 120.0);

    REQUIRE(clip.startTime == Catch::Approx(2.0));
    REQUIRE(clip.startBeats == Catch::Approx(4.0));
    REQUIRE(clip.length == Catch::Approx(5.0));
    REQUIRE(clip.lengthBeats == Catch::Approx(10.0));
}

TEST_CASE("ClipOperations - non-loop offset drag preserves sample start and clamps clip bounds",
          "[clip][audio][offset][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_offset_drag_regression.wav";
    clip.loopEnabled = false;
    clip.autoTempo = false;
    clip.startTime = 4.0;
    clip.length = 7.75;
    clip.setPlacementBeats(8.0, 15.5);
    clip.speedRatio = 1.0;
    clip.loopStart = 0.25;
    clip.offset = 0.25;

    ClipOperations::setAudioOffsetPreservingSourceRegion(clip, 0.75, 8.0, 120.0);

    REQUIRE(clip.offset == Catch::Approx(0.75));
    REQUIRE(clip.loopStart == Catch::Approx(0.25));
    REQUIRE(clip.startTime == Catch::Approx(4.0));
    REQUIRE(clip.length == Catch::Approx(7.25));
    REQUIRE(clip.lengthBeats == Catch::Approx(14.5));
}

TEST_CASE("ClipDisplayInfo - non-loop source end stays fixed when offset clamps clip length",
          "[clip][audio][offset][display][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_offset_display_regression.wav";
    clip.loopEnabled = false;
    clip.autoTempo = false;
    clip.startTime = 0.0;
    clip.length = 8.0;
    clip.speedRatio = 1.0;
    clip.offset = 0.0;

    ClipOperations::setAudioOffsetPreservingSourceRegion(clip, 1.0, 8.0, 120.0);

    const auto displayInfo = ClipDisplayInfo::from(clip, 120.0, 8.0);

    REQUIRE(clip.getTimelineLength(120.0) == Catch::Approx(7.0));
    REQUIRE(displayInfo.offsetPositionSeconds == Catch::Approx(1.0));
    REQUIRE(displayInfo.fileExtentTimeline() == Catch::Approx(8.0));
}

TEST_CASE("ClipOperations - non-loop right resize changes clip length only",
          "[clip][audio][resize][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_right_resize_regression.wav";
    clip.loopEnabled = false;
    clip.autoTempo = false;
    clip.startTime = 0.0;
    clip.length = 2.0;
    clip.setPlacementBeats(0.0, 4.0);
    clip.speedRatio = 1.0;
    clip.loopStart = 0.25;
    clip.offset = 0.5;

    ClipOperations::resizeContainerFromRight(clip, 3.0, 120.0);

    const auto displayInfo = ClipDisplayInfo::from(clip, 120.0, 8.0);

    REQUIRE(clip.length == Catch::Approx(3.0));
    REQUIRE(clip.lengthBeats == Catch::Approx(6.0));
    REQUIRE(clip.offset == Catch::Approx(0.5));
    REQUIRE(clip.loopStart == Catch::Approx(0.25));
    REQUIRE(displayInfo.offsetPositionSeconds + clip.getTimelineLength(120.0) ==
            Catch::Approx(3.5));
    REQUIRE(displayInfo.fileExtentTimeline() == Catch::Approx(8.0));
}

TEST_CASE("ClipOperations - phase drag clamps audio offset at zero",
          "[clip][audio][phase][regression]") {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "/tmp/magda_phase_drag_regression.wav";
    clip.loopEnabled = true;
    clip.autoTempo = false;
    clip.loopStart = -0.25;
    clip.offset = 0.0;

    ClipOperations::setAudioLoopPhaseClamped(clip, 0.1);

    REQUIRE(clip.offset == Catch::Approx(0.0));
}

TEST_CASE("SetClipOffsetCommand - non-loop offset clamp restores length on undo",
          "[clip][audio][offset][undo][regression]") {
    auto& clipManager = ClipManager::getInstance();
    clipManager.clearAllClips();

    ClipId clipId =
        clipManager.createAudioClip(INVALID_TRACK_ID, 0.0, 8.0, "/tmp/magda_offset_undo.wav");
    auto* clip = clipManager.getClip(clipId);
    REQUIRE(clip != nullptr);

    clip->loopEnabled = false;
    clip->autoTempo = false;
    clip->audio().source.durationSeconds = 8.0;
    clip->offset = 0.0;
    clip->length = 8.0;
    clip->setPlacementBeats(0.0, 16.0);

    SetClipOffsetCommand cmd(clipId, 1.0);
    cmd.execute();

    REQUIRE(clip->offset == Catch::Approx(1.0));
    REQUIRE(clip->length == Catch::Approx(7.0));

    cmd.undo();

    clip = clipManager.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->offset == Catch::Approx(0.0));
    REQUIRE(clip->length == Catch::Approx(8.0));
    REQUIRE(clip->lengthBeats == Catch::Approx(16.0));
}

TEST_CASE("ClipManager - Clip position change notifies listeners", "[clip][manager][notify]") {
    /**
     * This test verifies that when a clip's position changes,
     * ClipManager properly notifies listeners (which include the waveform editor)
     */

    // Reset ClipManager
    ClipManager::getInstance().shutdown();

    // Create a test listener to track notifications
    class TestListener : public ClipManagerListener {
      public:
        void clipsChanged() override {
            clipsChangedCount++;
        }
        void clipPropertyChanged(ClipId id) override {
            propertyChangedCount++;
            lastChangedClipId = id;
        }
        void clipSelectionChanged(ClipId) override {}

        int clipsChangedCount = 0;
        int propertyChangedCount = 0;
        ClipId lastChangedClipId = INVALID_CLIP_ID;
    };

    TestListener listener;
    ClipManager::getInstance().addListener(&listener);

    // Create a MIDI clip (doesn't require audio file path)
    ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0);
    REQUIRE(clipId != INVALID_CLIP_ID);
    REQUIRE(listener.clipsChangedCount == 1);

    // Get the clip and modify it
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->startTime == 0.0);

    // Move the clip (simulating drag on timeline)
    listener.propertyChangedCount = 0;
    ClipManager::getInstance().moveClip(clipId, 2.0);

    // Verify notification was sent
    REQUIRE(listener.propertyChangedCount >= 1);
    REQUIRE(listener.lastChangedClipId == clipId);

    // Verify clip position actually changed
    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->startTime == 2.0);

    ClipManager::getInstance().removeListener(&listener);
}
