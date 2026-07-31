#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"
#include "magda/daw/ui/state/TimelineEvents.hpp"

/**
 * Keyboard clip nudging (#1957)
 *
 * Drives TrackContentPanel's real nudge entry points — the ones the Shift+arrow
 * commands call — rather than the pure delta model underneath (that is covered
 * headless in test_clip_nudge.cpp). What is pinned here is everything the pure
 * model cannot see: which clips a nudge picks up, which tracks it steps
 * through, and that each press is a single undo step.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;

double startBeatOf(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    return clip ? clip->placement.startBeat : -1.0;
}

TrackId trackOf(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    return clip ? clip->trackId : INVALID_TRACK_ID;
}

void expectNear(juce::UnitTest& test, double actual, double expected, const juce::String& label) {
    constexpr double tolerance = 1.0e-6;
    test.expect(std::abs(actual - expected) <= tolerance, label + " expected " +
                                                              juce::String(expected, 6) + ", got " +
                                                              juce::String(actual, 6));
}

// A panel over three audio tracks with a group track in the middle of the
// stack, so vertical nudging has something it must step over.
struct NudgeFixture {
    TimelineController controller;
    TrackContentPanel panel;
    TrackId topTrack = INVALID_TRACK_ID;
    TrackId groupTrack = INVALID_TRACK_ID;
    TrackId middleTrack = INVALID_TRACK_ID;
    TrackId bottomTrack = INVALID_TRACK_ID;

    NudgeFixture() {
        auto& trackManager = TrackManager::getInstance();
        topTrack = trackManager.createTrack("Top", TrackType::Audio);
        groupTrack = trackManager.createTrack("Group", TrackType::Group);
        middleTrack = trackManager.createTrack("Middle", TrackType::Audio);
        bottomTrack = trackManager.createTrack("Bottom", TrackType::Audio);

        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
        panel.setController(&controller);

        // A fixed 1-beat grid: auto-grid would tie the nudge distance to zoom.
        // Leaving auto seeds the manual grid from the last auto-computed value
        // and ignores the requested fraction, so it takes a second dispatch —
        // now already manual — to pin the grid to 1/4.
        controller.dispatch(SetGridQuantizeEvent{false, 1, 4});
        controller.dispatch(SetGridQuantizeEvent{false, 1, 4});
        controller.dispatch(SetSnapEnabledEvent{true});
    }

    ClipId addClip(TrackId trackId, double startBeats, double lengthBeats = 4.0) {
        return ClipManager::getInstance().createMidiClipBeats(trackId, startBeats, lengthBeats,
                                                              ClipView::Arrangement);
    }
};

class ClipNudgeJuceTest final : public juce::UnitTest {
  public:
    ClipNudgeJuceTest() : juce::UnitTest("Clip Nudge Tests", "magda") {}

    void runTest() override {
        testHorizontalNudgeMovesSelection();
        testHorizontalNudgeKeepsSelectionSpacing();
        testHorizontalNudgeIsOneUndoStepPerPress();
        testUnselectedClipsStayPut();
        testVerticalNudgeStepsOverGroupTracks();
        testVerticalNudgeStopsAtTheEndOfTheStack();
        testVerticalNudgeMovesTheSelectionAsABlock();
    }

  private:
    void testHorizontalNudgeMovesSelection() {
        beginTest("Shift+left/right moves the selected clip one grid step (#1957)");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId clip = f.addClip(f.topTrack, 8.0);
            SelectionManager::getInstance().selectClips({clip});

            expect(f.panel.nudgeSelectedClipsHorizontally(1), "nudge later reported no move");
            expectNear(*this, startBeatOf(clip), 9.0, "start after nudging later");

            expect(f.panel.nudgeSelectedClipsHorizontally(-1), "nudge earlier reported no move");
            expectNear(*this, startBeatOf(clip), 8.0, "start after nudging back");
        });
    }

    void testHorizontalNudgeKeepsSelectionSpacing() {
        beginTest("A multi-clip nudge moves every clip by the same delta");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            // Deliberately off-grid: the anchor snaps to the grid and the rest
            // travel by its delta, so the gap between them survives.
            const ClipId early = f.addClip(f.topTrack, 0.5);
            const ClipId late = f.addClip(f.middleTrack, 6.25);
            SelectionManager::getInstance().selectClips({early, late});

            expect(f.panel.nudgeSelectedClipsHorizontally(1), "nudge reported no move");
            expectNear(*this, startBeatOf(early), 1.0, "anchor clip snapped to the grid");
            expectNear(*this, startBeatOf(late), 6.75, "trailing clip kept its offset");
        });
    }

    void testHorizontalNudgeIsOneUndoStepPerPress() {
        beginTest("Each nudge is its own undo step");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId clip = f.addClip(f.topTrack, 4.0);
            SelectionManager::getInstance().selectClips({clip});

            f.panel.nudgeSelectedClipsHorizontally(1);
            f.panel.nudgeSelectedClipsHorizontally(1);
            expectNear(*this, startBeatOf(clip), 6.0, "start after two nudges");

            auto& undoManager = UndoManager::getInstance();
            expect(undoManager.undo(), "first undo failed");
            expectNear(*this, startBeatOf(clip), 5.0, "one undo should step back once, not twice");
            expect(undoManager.undo(), "second undo failed");
            expectNear(*this, startBeatOf(clip), 4.0, "start after undoing both nudges");
        });
    }

    void testUnselectedClipsStayPut() {
        beginTest("A nudge only touches the selection");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId selected = f.addClip(f.topTrack, 4.0);
            const ClipId untouched = f.addClip(f.middleTrack, 4.0);
            SelectionManager::getInstance().selectClips({selected});

            f.panel.nudgeSelectedClipsHorizontally(1);
            expectNear(*this, startBeatOf(selected), 5.0, "selected clip start");
            expectNear(*this, startBeatOf(untouched), 4.0, "unselected clip start");
        });
    }

    void testVerticalNudgeStepsOverGroupTracks() {
        beginTest("Shift+down skips the group track (no clip timeline to land on)");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId clip = f.addClip(f.topTrack, 4.0);
            SelectionManager::getInstance().selectClips({clip});

            expect(f.panel.nudgeSelectedClipsToAdjacentTrack(1), "nudge down reported no move");
            expect(trackOf(clip) == f.middleTrack, "clip should land on the track past the group");

            expect(f.panel.nudgeSelectedClipsToAdjacentTrack(-1), "nudge up reported no move");
            expect(trackOf(clip) == f.topTrack, "clip should come back to the top track");
        });
    }

    void testVerticalNudgeStopsAtTheEndOfTheStack() {
        beginTest("A clip on the end track refuses to move off the stack");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId clip = f.addClip(f.topTrack, 4.0);
            SelectionManager::getInstance().selectClips({clip});

            expect(!f.panel.nudgeSelectedClipsToAdjacentTrack(-1),
                   "nudging above the first track should report no move");
            expect(trackOf(clip) == f.topTrack, "clip should not have moved");
        });
    }

    void testVerticalNudgeMovesTheSelectionAsABlock() {
        beginTest("A spread selection moves whole or not at all");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId upper = f.addClip(f.middleTrack, 4.0);
            const ClipId lower = f.addClip(f.bottomTrack, 4.0);
            SelectionManager::getInstance().selectClips({upper, lower});

            // The lower clip is already on the last track: moving down would
            // clamp it and collapse the pair onto one track, so nothing moves.
            expect(!f.panel.nudgeSelectedClipsToAdjacentTrack(1),
                   "nudging down should report no move");
            expect(trackOf(upper) == f.middleTrack, "upper clip should not have moved");
            expect(trackOf(lower) == f.bottomTrack, "lower clip should not have moved");

            // Up has room for both, so the pair travels together.
            expect(f.panel.nudgeSelectedClipsToAdjacentTrack(-1), "nudging up reported no move");
            expect(trackOf(upper) == f.topTrack, "upper clip should step up one track");
            expect(trackOf(lower) == f.middleTrack, "lower clip should step up one track");
        });
    }
};

ClipNudgeJuceTest clipNudgeJuceTest;

}  // namespace
