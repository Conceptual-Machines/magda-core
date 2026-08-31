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
        topTrack = trackManager.createTrack("Top", TrackType::Media);
        groupTrack = trackManager.createTrack("Group", TrackType::Group);
        middleTrack = trackManager.createTrack("Middle", TrackType::Media);
        bottomTrack = trackManager.createTrack("Bottom", TrackType::Media);

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
        testAdjacentSelectedClipsSurviveANudge();
        testAdjacentSelectedClipsSurviveAVerticalNudge();
        testFrozenTracksRefuseNudges();
        testSecondsModeFollowsTheDisplayedGrid();
        testHorizontalNudgeMovesSelection();
        testHorizontalNudgeKeepsSelectionSpacing();
        testHorizontalNudgeIsOneUndoStepPerPress();
        testUnselectedClipsStayPut();
        testVerticalNudgeStepsOverGroupTracks();
        testVerticalNudgeStopsAtTheEndOfTheStack();
        testVerticalNudgeMovesTheSelectionAsABlock();
    }

  private:
    // Each MoveClipCommand commits immediately and ClipManager resolves
    // overlaps against every other clip on the track, selected or not. Without
    // ordering the moves back-to-front, the leader lands on its still-unmoved
    // neighbour and deletes it — and which one survived came down to
    // unordered_set iteration order.
    void testAdjacentSelectedClipsSurviveANudge() {
        beginTest("Nudging two touching selected clips does not destroy either");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            const ClipId first = f.addClip(f.topTrack, 0.0, 4.0);   // 0..4
            const ClipId second = f.addClip(f.topTrack, 4.0, 4.0);  // 4..8
            SelectionManager::getInstance().selectClips({first, second});

            expect(f.panel.nudgeSelectedClipsHorizontally(1), "nudge later reported no move");
            expect(ClipManager::getInstance().getClip(first) != nullptr,
                   "leading clip was destroyed");
            expect(ClipManager::getInstance().getClip(second) != nullptr,
                   "trailing clip was destroyed by the clip moving into it");
            expectNear(*this, startBeatOf(first), 1.0, "leading clip start");
            expectNear(*this, startBeatOf(second), 5.0, "trailing clip start");

            // ...and the mirror case travelling the other way.
            expect(f.panel.nudgeSelectedClipsHorizontally(-1), "nudge earlier reported no move");
            expect(ClipManager::getInstance().getClip(first) != nullptr,
                   "leading clip was destroyed moving back");
            expect(ClipManager::getInstance().getClip(second) != nullptr,
                   "trailing clip was destroyed moving back");
            expectNear(*this, startBeatOf(first), 0.0, "leading clip start after moving back");
            expectNear(*this, startBeatOf(second), 4.0, "trailing clip start after moving back");
        });
    }

    void testAdjacentSelectedClipsSurviveAVerticalNudge() {
        beginTest("Moving a stacked selection across tracks does not destroy either clip");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            // Same time span on neighbouring tracks: moving the upper one down
            // lands it exactly on the lower one.
            const ClipId upper = f.addClip(f.topTrack, 4.0, 4.0);
            const ClipId lower = f.addClip(f.middleTrack, 4.0, 4.0);
            SelectionManager::getInstance().selectClips({upper, lower});

            expect(f.panel.nudgeSelectedClipsToAdjacentTrack(1), "nudge down reported no move");
            expect(ClipManager::getInstance().getClip(upper) != nullptr,
                   "upper clip was destroyed");
            expect(ClipManager::getInstance().getClip(lower) != nullptr,
                   "lower clip was destroyed by the clip moving onto it");
            expect(trackOf(upper) == f.middleTrack, "upper clip should have moved down one track");
            expect(trackOf(lower) == f.bottomTrack, "lower clip should have moved down one track");
        });
    }

    void testFrozenTracksRefuseNudges() {
        beginTest("Frozen tracks are refused as nudge sources and destinations");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            auto& trackManager = TrackManager::getInstance();
            const ClipId onFrozen = f.addClip(f.topTrack, 4.0);
            if (auto* track = trackManager.getTrack(f.topTrack))
                track->frozen = true;
            SelectionManager::getInstance().selectClips({onFrozen});

            expect(!f.panel.nudgeSelectedClipsHorizontally(1),
                   "a clip on a frozen track must not move along the timeline");
            expectNear(*this, startBeatOf(onFrozen), 4.0, "frozen clip start");
            expect(!f.panel.nudgeSelectedClipsToAdjacentTrack(1),
                   "a clip on a frozen track must not change track");
            expect(trackOf(onFrozen) == f.topTrack, "frozen clip track");

            // A frozen track is skipped as a destination too, so the clip below
            // it steps past rather than into it.
            if (auto* track = trackManager.getTrack(f.topTrack))
                track->frozen = false;
            if (auto* track = trackManager.getTrack(f.middleTrack))
                track->frozen = true;

            const ClipId mover = f.addClip(f.bottomTrack, 12.0);
            SelectionManager::getInstance().selectClips({mover});
            expect(f.panel.nudgeSelectedClipsToAdjacentTrack(-1), "nudge up reported no move");
            expect(trackOf(mover) == f.topTrack, "clip should skip over the frozen track");
        });
    }

    void testSecondsModeFollowsTheDisplayedGrid() {
        beginTest("Seconds display mode nudges by the second-based grid, not the beat fraction");

        magda::test::runWithCleanJuceState([this] {
            NudgeFixture f;
            // The two domains only disagree under auto-grid: a manual 1/4 grid
            // resolves to the same distance either way, so this must run with
            // auto-grid on, off 120 BPM, at a pinned zoom.
            //
            // 90 BPM, 40 px per beat: the seconds ladder picks the first
            // interval at least 50 px wide (60t >= 50 -> 1 s = 1.5 beats),
            // while the beat ladder picks the first power-of-two beat fraction
            // that fits (40f >= 50 -> 2 beats). 1.5 is a value no beat-fraction
            // grid can produce, so this only passes if the step came from the
            // domain the ruler is actually drawing.
            f.panel.setTempo(90.0);
            f.controller.dispatch(SetTempoEvent{90.0});
            f.controller.dispatch(SetZoomEvent{40.0});
            f.controller.dispatch(SetTimeDisplayModeEvent{TimeDisplayMode::Seconds});
            f.controller.dispatch(SetGridQuantizeEvent{true, 1, 4});

            const auto& state = f.controller.getState();
            expectNear(*this, state.getSnapInterval(), 1.0, "seconds grid interval");
            expectNear(*this, state.getSnapBeatFraction(), 2.0, "beat grid fraction");

            const ClipId clip = f.addClip(f.topTrack, 0.0);
            SelectionManager::getInstance().selectClips({clip});

            expect(f.panel.nudgeSelectedClipsHorizontally(1), "nudge reported no move");
            expectNear(*this, startBeatOf(clip), 1.5,
                       "clip should land on the next second-grid line, not the beat fraction");
        });
    }

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
