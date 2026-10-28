// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ViewModeController.hpp"
#include "magda/daw/ui/state/ModeContent.hpp"

using magda::ViewMode;
using magda::ViewModeController;

namespace {

/** @brief Counts notifications, and leaves the singleton as it found it. */
struct ModeFixture : magda::ViewModeListener {
    ViewModeController& controller = ViewModeController::getInstance();
    ViewMode startMode = controller.getViewMode();
    int notifications = 0;

    ModeFixture() {
        controller.setViewMode(ViewMode::Arrange);
        controller.addListener(this);
    }
    ~ModeFixture() override {
        controller.removeListener(this);
        controller.setExclusive(ViewMode::Master, false);
        controller.setViewMode(startMode);
    }
    void viewModeChanged(ViewMode, const magda::AudioEngineProfile&) override {
        ++notifications;
    }
};

}  // namespace

TEST_CASE("An exclusive mode refuses every other mode until it is exited", "[view-modes][3077]") {
    ModeFixture f;
    f.controller.setExclusive(ViewMode::Master, true);
    f.controller.setViewMode(ViewMode::Live);
    f.controller.setViewMode(ViewMode::Master);
    REQUIRE(f.controller.getViewMode() == ViewMode::Master);

    for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix}) {
        CHECK_FALSE(f.controller.canSwitchTo(mode));
        f.controller.setViewMode(mode);
        CHECK(f.controller.getViewMode() == ViewMode::Master);
    }
    CHECK(f.notifications == 2);

    f.controller.exitExclusiveMode();
    CHECK(f.controller.getViewMode() == ViewMode::Live);
    CHECK(f.notifications == 3);
}

TEST_CASE("Leaving a mode that is not exclusive does nothing", "[view-modes][3077]") {
    ModeFixture f;
    f.controller.exitExclusiveMode();
    CHECK(f.controller.getViewMode() == ViewMode::Arrange);
    CHECK(f.notifications == 0);
    CHECK(f.controller.canSwitchTo(ViewMode::Mix));
}

TEST_CASE("Master without registered content is not exclusive", "[view-modes][3077]") {
    ModeFixture f;
    CHECK(magda::ModeContentRegistry::getInstance().find(ViewMode::Master) == nullptr);
    f.controller.setViewMode(ViewMode::Master);
    f.controller.setViewMode(ViewMode::Mix);
    CHECK(f.controller.getViewMode() == ViewMode::Mix);
}
