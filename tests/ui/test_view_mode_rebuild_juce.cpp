// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/ViewModeController.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/utils/TrackNamesSnapshot.hpp"

/**
 * The arrangement rebuilds for the modes it is on screen in, and only those.
 *
 * Every view used to rebuild its rows for whichever mode was picked, hidden or not, which on
 * a 150-track project held the message thread for seconds per switch.
 */

using namespace magda;

namespace {

class ViewModeRebuildTest final : public juce::UnitTest {
  public:
    ViewModeRebuildTest() : juce::UnitTest("View Mode Rebuild Tests", "magda") {}

    void runTest() override {
        test::runWithCleanJuceState([this] { testArrangementIgnoresTheMixer(); });
        test::runWithCleanJuceState([this] { testRenameIsSeenWhenShown(); });
    }

  private:
    void testRenameIsSeenWhenShown() {
        beginTest("A view kept built while hidden sees a rename it was not told about");

        auto& trackManager = TrackManager::getInstance();
        const auto renamed = trackManager.createTrack("Bass", TrackType::Media);
        trackManager.createTrack("Send", TrackType::Media);

        TrackNamesSnapshot names;
        names.take();
        expect(!names.isStale(), "nothing has moved");

        trackManager.setTrackName(renamed, "Sub");
        expect(names.isStale(), "the rename is seen");

        names.take();
        trackManager.createTrack("Third", TrackType::Media);
        expect(names.isStale(), "so is a new track");
    }

    void testArrangementIgnoresTheMixer() {
        beginTest("The arrangement keeps its rows through the mixer and follows Master");

        auto& modes = ViewModeController::getInstance();
        modes.setViewMode(ViewMode::Arrange);

        auto& trackManager = TrackManager::getInstance();
        const auto shown = trackManager.createTrack("Shown everywhere", TrackType::Media);
        // Master hides tracks unless told otherwise.
        trackManager.setTrackVisible(shown, ViewMode::Master, true);
        const auto hidden = trackManager.createTrack("Hidden in Mix and Master", TrackType::Media);
        trackManager.setTrackVisible(hidden, ViewMode::Mix, false);

        TrackContentPanel panel;
        panel.setSize(2000, 400);
        expectEquals(panel.getNumTracks(), 2, "both tracks have rows in Arrange");

        modes.setViewMode(ViewMode::Mix);
        expectEquals(panel.getNumTracks(), 2, "the mixer's settings are not the arrangement's");

        modes.setViewMode(ViewMode::Master);
        expectEquals(panel.getNumTracks(), 1, "Master is on the timeline, so it rebuilds");

        modes.setViewMode(ViewMode::Arrange);
        expectEquals(panel.getNumTracks(), 2, "and back");
    }
};

ViewModeRebuildTest viewModeRebuildTest;

}  // namespace
