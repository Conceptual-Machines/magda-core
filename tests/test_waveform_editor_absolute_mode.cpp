#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"

/**
 * Tests for waveform editor absolute mode positioning logic
 *
 * Bug fixed: When a clip was moved on the timeline, the waveform editor
 * in absolute (ABS) mode would show the wrong position because
 * clipStartTime_ wasn't being updated in WaveformGridComponent.
 *
 * Example: Clip at bar 1-3 (2 bars), move to bar 2-4
 * - Before fix: waveform editor showed bars 1-3 (old position)
 * - After fix: waveform editor shows bars 2-4 (correct position)
 *
 * These tests verify the coordinate conversion logic that the fix relies on.
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
          "[waveform][grid][absolute]") {
    WaveformCoordinateConverter converter;

    SECTION("Initial state has zero clip position") {
        converter.setRelativeMode(false);
        converter.setHorizontalZoom(100.0);

        REQUIRE(converter.getClipStartTime() == 0.0);
        REQUIRE(converter.getClipLength() == 0.0);

        int pixelAt0 = converter.timeToPixel(0.0);
        REQUIRE(pixelAt0 == LEFT_PADDING);
    }

    SECTION("updateClipPosition updates start time") {
        converter.setRelativeMode(false);
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
        converter.setRelativeMode(false);
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
    converter.setRelativeMode(false);
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
    converter.setRelativeMode(false);

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

TEST_CASE("WaveformCoordinateConverter - Clip move bug scenario", "[waveform][grid][regression]") {
    /**
     * This test reproduces the exact bug that was fixed:
     *
     * 1. User creates audio clip at bar 1-3 (2 bars at 120 BPM = 4 seconds)
     * 2. Opens waveform editor in ABS mode
     * 3. Moves clip to bar 2-4 on the timeline
     * 4. BUG: Waveform editor still showed bars 1-3 instead of bars 2-4
     *
     * Root cause: clipPropertyChanged() wasn't calling updateClipPosition()
     * on the grid component, so clipStartTime_ kept the old value.
     *
     * Fix: Added updateClipPosition() call in clipPropertyChanged()
     */

    WaveformCoordinateConverter converter;
    converter.setRelativeMode(false);  // Absolute mode - critical for this bug
    converter.setHorizontalZoom(100.0);

    constexpr double BAR_DURATION = 2.0;  // At 120 BPM

    // Step 1: Clip at bar 1-3 (0s to 4s)
    double clipStartBar1 = 0.0 * BAR_DURATION;
    double clipLength = 2.0 * BAR_DURATION;  // 2 bars = 4 seconds
    converter.updateClipPosition(clipStartBar1, clipLength);

    int pixelAtBar1 = converter.timeToPixel(clipStartBar1);
    int pixelAtBar3 = converter.timeToPixel(clipStartBar1 + clipLength);

    REQUIRE(converter.getClipStartTime() == 0.0);
    REQUIRE(pixelAtBar1 == 10);   // Bar 1 at pixel 10
    REQUIRE(pixelAtBar3 == 410);  // Bar 3 at pixel 410

    // Step 2: User moves clip to bar 2-4 (2s to 6s)
    // THIS IS THE CRITICAL STEP - the bug was that this wasn't being called
    double clipStartBar2 = 1.0 * BAR_DURATION;  // Bar 2 = 2 seconds
    converter.updateClipPosition(clipStartBar2, clipLength);

    // Step 3: Verify the converter now reflects the NEW position

    // Clip start should be updated
    REQUIRE(converter.getClipStartTime() == 2.0);

    // Pixel positions should reflect the new clip position
    int pixelAtNewStart = converter.timeToPixel(clipStartBar2);
    int pixelAtNewEnd = converter.timeToPixel(clipStartBar2 + clipLength);

    REQUIRE(pixelAtNewStart == 210);  // Bar 2 at pixel 210
    REQUIRE(pixelAtNewEnd == 610);    // Bar 4 at pixel 610

    // The bug was that pixelAtNewStart would still be 10 (bar 1)
    // because clipStartTime_ wasn't updated
    REQUIRE(pixelAtNewStart != pixelAtBar1);
    REQUIRE(pixelAtNewEnd != pixelAtBar3);
}

TEST_CASE("WaveformCoordinateConverter - Multiple moves preserve correct position",
          "[waveform][grid][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(false);
    converter.setHorizontalZoom(100.0);

    double clipLength = 4.0;  // 2 bars at 120 BPM

    // Move clip through several positions
    std::vector<double> positions = {0.0, 2.0, 4.0, 8.0, 2.0, 0.0};

    for (double startTime : positions) {
        converter.updateClipPosition(startTime, clipLength);

        // Verify internal state
        REQUIRE(converter.getClipStartTime() == startTime);
        REQUIRE(converter.getClipLength() == clipLength);

        // Verify coordinate conversion
        int expectedPixelStart = static_cast<int>(startTime * 100.0) + LEFT_PADDING;
        int expectedPixelEnd = static_cast<int>((startTime + clipLength) * 100.0) + LEFT_PADDING;

        REQUIRE(converter.timeToPixel(startTime) == expectedPixelStart);
        REQUIRE(converter.timeToPixel(startTime + clipLength) == expectedPixelEnd);
    }
}

TEST_CASE("WaveformCoordinateConverter - offset moves independently from sample start",
          "[waveform][grid][offset][regression]") {
    WaveformCoordinateConverter converter;
    converter.setRelativeMode(false);
    converter.setHorizontalZoom(100.0);
    converter.updateClipPosition(4.0, 2.0);
    converter.updateSourcePositions(1.0, 1.0);

    const int initialSampleStartX = converter.sampleStartPixel();
    const int initialOffsetX = converter.offsetPixel();

    REQUIRE(initialSampleStartX == 410);
    REQUIRE(initialOffsetX == initialSampleStartX);

    converter.updateSourcePositions(1.0, 1.5);

    REQUIRE(converter.sampleStartPixel() == initialSampleStartX);
    REQUIRE(converter.offsetPixel() == 460);
    REQUIRE(converter.offsetPixel() != initialOffsetX);
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
