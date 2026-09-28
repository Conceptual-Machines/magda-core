#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/core/GridDivision.hpp"
#include "../../magda/daw/ui/components/common/GridDivisionMenu.hpp"
#include "../../magda/daw/ui/state/TimelineController.hpp"
#include "../../magda/daw/ui/state/TimelineEvents.hpp"
#include "../../magda/daw/ui/state/TimelineState.hpp"

using magda::GridConstants;
using magda::GridQuantize;

TEST_CASE("Grid division labels use reduced stored fractions", "[grid_constants][grid_division]") {
    using namespace magda::daw::ui;
    REQUIRE(gridDivisionLabel(4, 16) == "1/4");
    REQUIRE(gridDivisionLabel(2, 12) == "1/4T");
    REQUIRE(gridDivisionLabel(32, 3) == "32 / 3");
}

TEST_CASE("Grid division normalisation clamps and reduces custom values",
          "[grid_constants][grid_division]") {
    REQUIRE(magda::grid::normaliseFraction(4, 2) == std::pair{2, 1});
    REQUIRE(magda::grid::normaliseFraction(0, 0) == std::pair{1, 1});
    REQUIRE(magda::grid::normaliseFraction(2000, 128) == std::pair{999, 64});
}

// ============================================================================
// gridAlignsWithBars
// ============================================================================

TEST_CASE("gridAlignsWithBars - 1/4 note (1.0 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(1.0, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/8 note (0.5 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.5, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/16 note (0.25 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.25, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 3/16 note (0.75 beat) does NOT align in 4/4", "[grid_constants]") {
    // fmod(4.0, 0.75) ≈ 0.25, which is not near 0 or 0.75
    REQUIRE(GridConstants::gridAlignsWithBars(0.75, 4.0) == false);
}

TEST_CASE("gridAlignsWithBars - 1/6 note (0.667 beat) aligns with bars (6 fit in 4 beats)",
          "[grid_constants]") {
    double interval = 4.0 / 6.0;  // ~0.6667, divides 4.0 evenly
    REQUIRE(GridConstants::gridAlignsWithBars(interval, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 0.3 beats does NOT align in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.3, 4.0) == false);
}

TEST_CASE("gridAlignsWithBars - 2 bars (8.0 beats) aligns in 4/4", "[grid_constants]") {
    // intervalBeats >= barLengthBeats → true
    REQUIRE(GridConstants::gridAlignsWithBars(8.0, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/4 note in 3/4 time", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(1.0, 3.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/8 note in 3/4 time", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.5, 3.0) == true);
}

// ============================================================================
// gridAlignsWithBeats
// ============================================================================

TEST_CASE("gridAlignsWithBeats - 1/4 note (1.0 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(1.0) == true);
}

TEST_CASE("gridAlignsWithBeats - 1/8 note (0.5 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.5) == true);
}

TEST_CASE("gridAlignsWithBeats - 1/16 note (0.25 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.25) == true);
}

TEST_CASE("gridAlignsWithBeats - 3/16 note (0.75 beat) does NOT align", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.75) == false);
}

TEST_CASE("gridAlignsWithBeats - 1/6 note (0.667 beat) does NOT align with beats",
          "[grid_constants]") {
    double interval = 4.0 / 6.0;  // ~0.6667, does NOT divide 1.0 evenly
    REQUIRE(GridConstants::gridAlignsWithBeats(interval) == false);
}

TEST_CASE("gridAlignsWithBeats - 0.3 beats does NOT align", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.3) == false);
}

TEST_CASE("gridAlignsWithBeats - 2 beats (1/2 note)", "[grid_constants]") {
    // intervalBeats >= 1.0 → true
    REQUIRE(GridConstants::gridAlignsWithBeats(2.0) == true);
}

// ============================================================================
// classifyBeatPosition
// ============================================================================

TEST_CASE("classifyBeatPosition - beat 0.0 is bar and beat start in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.0, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 1.0 is beat but not bar in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(1.0, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 4.0 is bar start in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(4.0, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 0.5 is subdivision only", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.5, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == false);
}

TEST_CASE("classifyBeatPosition - beat 3.9999 is bar start (two-sided tolerance)",
          "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(3.9999, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 0.9999 is beat start (two-sided tolerance)",
          "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.9999, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 3.0 in 3/4 is bar start", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(3.0, 3.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 2.0 in 3/4 is beat, not bar", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(2.0, 3.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

// ============================================================================
// computeGridInterval
// ============================================================================

TEST_CASE("computeGridInterval - manual mode 1/8 returns 0.5", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = false;
    gq.numerator = 1;
    gq.denominator = 8;
    REQUIRE(GridConstants::computeGridInterval(gq, 100.0, 4, 1.0, 10) == Catch::Approx(0.5));
}

TEST_CASE("computeGridInterval - manual mode 3/16 returns 0.75", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = false;
    gq.numerator = 3;
    gq.denominator = 16;
    REQUIRE(GridConstants::computeGridInterval(gq, 100.0, 4, 1.0, 10) == Catch::Approx(0.75));
}

TEST_CASE("computeGridInterval - auto mode high zoom returns beat subdivision",
          "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = true;
    // zoom=100 ppb, minPixels=10 → 0.125 beats * 100 = 12.5px ≥ 10 → should pick a small
    // subdivision
    double interval = GridConstants::computeGridInterval(gq, 100.0, 4, 1.0, 10);
    REQUIRE(interval > 0.0);
    REQUIRE(interval <= 1.0);  // Should be a beat subdivision, not bar multiple
}

TEST_CASE("computeGridInterval - auto mode low zoom falls to bar multiples", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = true;
    // zoom=1 ppb, minPixels=10 → even 2 beats * 1 = 2px < 10, so must go to bar multiples
    double interval = GridConstants::computeGridInterval(gq, 1.0, 4, 1.0, 10);
    // Should be a bar multiple: timeSigNumerator * mult
    REQUIRE(interval >= 4.0);  // At least 1 bar in 4/4
}

TEST_CASE("TimelineState max scroll clamps to real timeline width", "[timeline][zoom][scroll]") {
    magda::TimelineState state;
    state.tempo.bpm = 120.0;
    state.tempo.timeSignatureNumerator = 4;
    state.timelineLength = 1024.0;  // 512 bars at 120 BPM
    state.timelineLengthBeats = state.secondsToBeats(state.timelineLength);
    state.zoom.viewportWidth = 1600;

    SECTION("zoomed out timeline cannot scroll into synthetic padding") {
        state.zoom.horizontalZoom = 0.25;  // 512 px of actual timeline

        REQUIRE(state.getContentWidth() > state.zoom.viewportWidth);
        REQUIRE(state.getMaxScrollX() == 0);
    }

    SECTION("zoomed in timeline still scrolls to the real end") {
        state.zoom.horizontalZoom = 2.0;  // 4096 px of actual timeline

        REQUIRE(state.getMaxScrollX() > 0);
        REQUIRE(state.getMaxScrollX() ==
                static_cast<int>(std::round(state.secondsToBeats(state.timelineLength) *
                                            state.zoom.horizontalZoom)) +
                    magda::LayoutConfig::TIMELINE_LEFT_PADDING - state.zoom.viewportWidth);
    }
}

TEST_CASE("TimelineState minimum zoom fits the full timeline with label gutter",
          "[timeline][zoom][scroll]") {
    magda::TimelineState state;
    state.tempo.bpm = 120.0;
    state.timelineLength = 1024.0;
    state.timelineLengthBeats = state.secondsToBeats(state.timelineLength);
    state.zoom.viewportWidth = 1600;

    const double expectedAvailableWidth =
        static_cast<double>(state.zoom.viewportWidth - magda::LayoutConfig::TIMELINE_LEFT_PADDING) -
        magda::TimelineState::MIN_ZOOM_RIGHT_LABEL_GUTTER;

    REQUIRE(state.getMinZoom() ==
            Catch::Approx(expectedAvailableWidth / state.timelineLengthBeats));

    state.zoom.horizontalZoom = state.getMinZoom();
    const int timelineEndX =
        static_cast<int>(std::round(state.timelineLengthBeats * state.zoom.horizontalZoom)) +
        magda::LayoutConfig::TIMELINE_LEFT_PADDING;

    REQUIRE(timelineEndX ==
            state.zoom.viewportWidth -
                static_cast<int>(magda::TimelineState::MIN_ZOOM_RIGHT_LABEL_GUTTER));
    REQUIRE(state.getMaxScrollX() == 0);
}

TEST_CASE("TimelineController clamps zoom when viewport establishes a larger minimum",
          "[timeline][zoom][scroll]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTimelineLengthBeatsEvent{2048.0});
    controller.dispatch(magda::ViewportResizedEvent{800, 600});
    controller.dispatch(magda::SetZoomEvent{0.01});

    const double smallViewportZoom = controller.getState().zoom.horizontalZoom;

    controller.dispatch(magda::ViewportResizedEvent{2000, 600});

    const auto& state = controller.getState();
    REQUIRE(state.zoom.horizontalZoom > smallViewportZoom);
    REQUIRE(state.zoom.horizontalZoom == Catch::Approx(state.getMinZoom()));
}

TEST_CASE("Timeline edit cursor is beat-authoritative across tempo changes",
          "[timeline][edit-cursor][tempo]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTempoEvent{120.0});
    controller.dispatch(magda::SetTimelineLengthEvent{100.0});
    controller.dispatch(magda::SetEditCursorEvent{8.0});

    const auto& state = controller.getState();
    REQUIRE(state.editCursorBeats == Catch::Approx(8.0));
    REQUIRE(state.editCursorPosition == Catch::Approx(4.0));

    controller.dispatch(magda::SetTempoEvent{60.0});
    REQUIRE(state.editCursorBeats == Catch::Approx(8.0));
    REQUIRE(state.editCursorPosition == Catch::Approx(8.0));

    controller.dispatch(magda::SetEditCursorEvent{-1.0});
    REQUIRE(state.editCursorBeats == Catch::Approx(-1.0));
    REQUIRE(state.editCursorPosition == Catch::Approx(-1.0));
}

// ============================================================================
// The denominator sets the bar length in quarter-note beats (#2802)
// ============================================================================

TEST_CASE("A bar is numerator times the denominator's note", "[grid_constants][signature]") {
    REQUIRE(magda::beatsPerBar(4, 4) == Catch::Approx(4.0));
    REQUIRE(magda::beatsPerBar(6, 4) == Catch::Approx(6.0));
    REQUIRE(magda::beatsPerBar(6, 8) == Catch::Approx(3.0));
    REQUIRE(magda::beatsPerBar(7, 8) == Catch::Approx(3.5));
    REQUIRE(magda::beatsPerBar(2, 2) == Catch::Approx(4.0));
    REQUIRE(magda::signatureBeatLength(8) == Catch::Approx(0.5));
}

TEST_CASE("Bars.beats.ticks counts the signature's beats", "[grid_constants][signature]") {
    auto p = magda::toBarsBeatsTicks(3.0, 6, 8);
    REQUIRE(p.bars == 1);
    REQUIRE(p.beats == 0);
    REQUIRE(p.ticks == 0);

    p = magda::toBarsBeatsTicks(1.75, 6, 8);
    REQUIRE(p.bars == 0);
    REQUIRE(p.beats == 3);
    REQUIRE(p.ticks == 240);
    REQUIRE(magda::fromBarsBeatsTicks(p, 6, 8) == Catch::Approx(1.75));

    p = magda::toBarsBeatsTicks(5.0, 4, 4);
    REQUIRE(p.bars == 1);
    REQUIRE(p.beats == 1);
    REQUIRE(p.ticks == 0);
}

TEST_CASE("classifyBeatPosition in 6/8 marks bars every 3 beats and beats every eighth",
          "[grid_constants][signature]") {
    const double bar = magda::beatsPerBar(6, 8);
    const double beat = magda::signatureBeatLength(8);
    REQUIRE(GridConstants::classifyBeatPosition(3.0, bar, beat).isBar);
    REQUIRE_FALSE(GridConstants::classifyBeatPosition(6.0 - 4.5, bar, beat).isBar);
    REQUIRE(GridConstants::classifyBeatPosition(1.5, bar, beat).isBeat);
    REQUIRE_FALSE(GridConstants::classifyBeatPosition(0.25, bar, beat).isBeat);
}

TEST_CASE("Timeline bars in 6/8 are three quarter notes long", "[timeline][signature]") {
    magda::TimelineController controller;
    controller.dispatch(magda::SetTempoEvent{120.0});
    controller.dispatch(magda::SetTimeSignatureEvent{6, 8});

    const auto& tempo = controller.getState().tempo;
    REQUIRE(tempo.beatsPerBar() == Catch::Approx(3.0));
    REQUIRE(tempo.getSecondsPerBar() == Catch::Approx(1.5));
    REQUIRE(controller.getState().formatTimePosition(1.5) == "2.1.1");
    REQUIRE(controller.getState().formatTimePosition(2.25) == "2.4.1");
}

TEST_CASE("The auto grid in 4/5 subdivides the fifth-note beat", "[grid_constants][signature]") {
    const double bar = magda::beatsPerBar(4, 5);
    const double beat = magda::signatureBeatLength(5);
    GridQuantize gq;
    gq.autoGrid = true;

    // 149 px per quarter note, as in the arrangement report: half a fifth-note clears 50 px.
    const double interval = GridConstants::computeGridInterval(gq, 149.0, bar, beat, 50);
    REQUIRE(interval == Catch::Approx(0.4));
    REQUIRE(GridConstants::gridAlignsWithBars(interval, bar));
    REQUIRE(GridConstants::gridAlignsWithBeats(interval, beat));
    REQUIRE(GridConstants::noteFraction(interval) == std::pair{1, 10});
    REQUIRE(GridConstants::noteFraction(beat) == std::pair{1, 5});
    REQUIRE(GridConstants::noteFraction(0.25) == std::pair{1, 16});
}
