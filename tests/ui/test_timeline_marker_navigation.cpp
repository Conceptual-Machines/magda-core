// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/state/TimelineController.hpp"

using Catch::Approx;
using magda::GoToNextMarkerEvent;
using magda::GoToPreviousMarkerEvent;
using magda::SetEditPositionBeatsEvent;

namespace {

double editBeats(const magda::TimelineController& controller) {
    return controller.getState().playhead.editPositionBeats;
}

}  // namespace

TEST_CASE("Previous and Next step between markers and stop at the timeline ends (#3280)",
          "[timeline][markers]") {
    magda::TimelineController controller;
    const double endBeats = controller.getState().timelineLengthBeats;
    REQUIRE(endBeats > 16.0);

    controller.dispatch(magda::AddMarkerBeatsEvent{8.0, "A"});
    controller.dispatch(magda::AddMarkerBeatsEvent{16.0, "B"});
    controller.dispatch(SetEditPositionBeatsEvent{4.0});

    controller.dispatch(GoToNextMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(8.0));
    controller.dispatch(GoToNextMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(16.0));
    controller.dispatch(GoToNextMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(endBeats));
    controller.dispatch(GoToNextMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(endBeats));

    controller.dispatch(GoToPreviousMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(16.0));
    controller.dispatch(GoToPreviousMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(8.0));
    controller.dispatch(GoToPreviousMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(0.0));
    controller.dispatch(GoToPreviousMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(0.0));
}

TEST_CASE("Previous and Next go to the start and end when there are no markers (#3280)",
          "[timeline][markers]") {
    magda::TimelineController controller;
    const double endBeats = controller.getState().timelineLengthBeats;
    controller.dispatch(SetEditPositionBeatsEvent{12.0});

    controller.dispatch(GoToNextMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(endBeats));

    controller.dispatch(GoToPreviousMarkerEvent{});
    REQUIRE(editBeats(controller) == Approx(0.0));
}
