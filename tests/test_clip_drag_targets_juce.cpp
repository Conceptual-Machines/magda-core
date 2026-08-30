#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"
#include "magda/daw/ui/state/TimelineEvents.hpp"

/**
 * Clip drag destinations (#2179)
 *
 * A clip dragged over a lane that cannot hold it used to follow the pointer
 * onto that lane, show a ghost there and be declined on release: the gesture
 * said yes for as long as it lasted and said no at the only moment the user
 * could no longer change their mind.
 *
 * What is pinned here is that the drag steps over such a lane the way the
 * keyboard nudge already does, and -- the part that made the old behaviour a
 * bug rather than a preference -- that the preview and the release agree. The
 * pure index rules underneath are covered headless in
 * test_clip_drag_targets.cpp; this drives the panel's real drag entry points
 * over a stack with a refusing lane in the middle of it.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;

TrackId trackOf(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    return clip ? clip->trackId : INVALID_TRACK_ID;
}

// Media / Chord / Media / Media, so a plain MIDI clip on the top track has a
// lane it cannot land on directly beneath it and two it can below that. The
// chord track takes progressions only, and a clip created here has no chord
// annotations, so it is refused with the clip in hand rather than by kind.
struct DragFixture {
    TimelineController controller;
    TrackContentPanel panel;
    TrackId topTrack = INVALID_TRACK_ID;
    TrackId chordTrack = INVALID_TRACK_ID;
    TrackId middleTrack = INVALID_TRACK_ID;
    TrackId bottomTrack = INVALID_TRACK_ID;

    DragFixture() {
        auto& trackManager = TrackManager::getInstance();
        topTrack = trackManager.createTrack("Top", TrackType::Media);
        chordTrack = trackManager.createTrack("Chords", TrackType::Chord);
        middleTrack = trackManager.createTrack("Middle", TrackType::Media);
        bottomTrack = trackManager.createTrack("Bottom", TrackType::Media);

        panel.setSize(2000, 600);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
        panel.setController(&controller);
        controller.dispatch(SetSnapEnabledEvent{false});
    }

    /// Open an automation lane under @p trackId, so its row is taller than its
    /// track and there is a band of Y inside the stack that is not any track's
    /// own lane area.
    AutomationLaneId showAutomationLaneOn(TrackId trackId) {
        AutomationTarget target;
        target.kind = ControlTarget::Kind::TrackVolume;
        target.devicePath = ChainNodePath::trackLevel(trackId);
        const auto laneId =
            AutomationManager::getInstance().createLane(target, AutomationLaneType::Absolute);
        AutomationManager::getInstance().setLaneVisible(laneId, true);
        return laneId;
    }

    ClipId addClip(TrackId trackId, double startBeats, double lengthBeats = 4.0) {
        return ClipManager::getInstance().createMidiClipBeats(trackId, startBeats, lengthBeats,
                                                              ClipView::Arrangement);
    }

    /// A pointer position in the middle of a lane, which is where a user
    /// dragging onto that lane leaves the mouse.
    int centreOfLane(int lane) const {
        return panel.getTrackYPosition(lane) + panel.getTrackTotalHeight(lane) / 2;
    }

    // Asked of the panel rather than assumed from creation order: display
    // order is the panel's to decide, and a test that hard-codes it is testing
    // its own arithmetic.
    TrackId trackIdAtLane(int lane) const {
        const auto& lanes = panel.getVisibleTrackIds();
        return (lane >= 0 && lane < static_cast<int>(lanes.size()))
                   ? lanes[static_cast<size_t>(lane)]
                   : INVALID_TRACK_ID;
    }

    int laneOf(TrackId trackId) const {
        const auto& lanes = panel.getVisibleTrackIds();
        for (int lane = 0; lane < static_cast<int>(lanes.size()); ++lane)
            if (lanes[static_cast<size_t>(lane)] == trackId)
                return lane;
        return -1;
    }
};

}  // namespace

class ClipDragTargetsJuceTest final : public juce::UnitTest {
  public:
    ClipDragTargetsJuceTest() : juce::UnitTest("Clip Drag Targets Tests", "magda") {}

    void runTest() override {
        testDragStepsOverARefusingLane();
        testPreviewAndReleaseAgree();
        testSelectionKeepsItsSpreadAcrossARefusingLane();
        testDragPinsWhenNoLaneAccepts();
        testFrozenLanesAreSteppedOver();
        testAnAutomationLaneBelongsToItsOwnTrack();
    }

  private:
    // The ghost the drag is showing, expressed as the lane it sits on, which is
    // the thing the user is reading off the screen.
    int ghostLane(const DragFixture& f, ClipId clipId) {
        const auto bounds = f.panel.getClipGhostBounds(clipId);
        if (bounds.isEmpty())
            return -1;
        return f.panel.getTrackIndexAtY(bounds.getCentreY());
    }

    void testDragStepsOverARefusingLane() {
        beginTest("A drag onto a lane that cannot hold the clip lands past it");

        magda::test::runWithCleanJuceState([this] {
            DragFixture f;
            const ClipId clip = f.addClip(f.topTrack, 0.0);
            SelectionManager::getInstance().selectClip(clip);

            // The premise, asserted rather than assumed: a refusing lane with a
            // usable one either side of it. Without this the case degenerates
            // into clamping at the end of the stack, which is a different rule
            // and would pass while testing nothing.
            expect(f.laneOf(f.topTrack) < f.laneOf(f.chordTrack) &&
                       f.laneOf(f.chordTrack) < f.laneOf(f.middleTrack),
                   "the chord lane should sit between the two media lanes, got " +
                       juce::String(f.laneOf(f.topTrack)) + "/" +
                       juce::String(f.laneOf(f.chordTrack)) + "/" +
                       juce::String(f.laneOf(f.middleTrack)));

            // The panel resolves the destination for both the ghost and the
            // commit; a single-clip drag asks it exactly this way.
            f.panel.beginClipDragTargets({clip}, clip);

            // Pointer on the chord lane, which cannot hold a plain MIDI clip.
            const int delta = f.panel.clipDragSlotDelta(f.centreOfLane(f.laneOf(f.chordTrack)));
            const TrackId target = f.panel.clipDragTargetTrackId(f.topTrack, delta);
            expect(target != f.chordTrack, "the chord track must never be a destination");
            expect(target == f.topTrack,
                   "halfway onto the refusing lane the clip stays on its own track");

            // Pointer on the lane below it: the usable lane the step-over reaches.
            const int pastDelta =
                f.panel.clipDragSlotDelta(f.centreOfLane(f.laneOf(f.middleTrack)));
            expect(f.panel.clipDragTargetTrackId(f.topTrack, pastDelta) == f.middleTrack,
                   "the drag should reach the track below the chord track");
        });
    }

    void testPreviewAndReleaseAgree() {
        beginTest("The lane the ghost sits on at release is the lane the clip lands on");

        // Every lane in the stack, including the refusing one, because the
        // disagreement this replaced only appeared over that lane. Each pass
        // gets its own clean state: sharing one would stack a second set of
        // tracks under the first and leave the lane indices meaning something
        // else on every iteration after the first.
        for (int lane = 0; lane < 4; ++lane) {
            magda::test::runWithCleanJuceState([this, lane] {
                DragFixture f;
                expect(static_cast<int>(f.panel.getVisibleTrackIds().size()) == 4,
                       "the fixture should be four lanes, got " +
                           juce::String(static_cast<int>(f.panel.getVisibleTrackIds().size())));

                const ClipId clip = f.addClip(f.topTrack, 0.0);
                const ClipId partner = f.addClip(f.middleTrack, 32.0);
                SelectionManager::getInstance().selectClips({clip, partner});

                f.panel.startMultiClipDrag(clip, {100, f.centreOfLane(f.laneOf(f.topTrack))});
                f.panel.updateMultiClipDrag({100, f.centreOfLane(lane)});

                const int previewLane = ghostLane(f, clip);
                f.panel.finishMultiClipDrag();

                const TrackId landed = trackOf(clip);
                if (previewLane < 0) {
                    expect(landed == f.topTrack,
                           "no ghost means the clip did not move, lane " + juce::String(lane));
                } else {
                    expect(f.trackIdAtLane(previewLane) == landed,
                           "ghost lane " + juce::String(previewLane) +
                               " disagreed with the committed track, pointer on lane " +
                               juce::String(lane));
                }
                expect(landed != f.chordTrack,
                       "a plain MIDI clip must never land on the chord track");
            });
        }
    }

    void testSelectionKeepsItsSpreadAcrossARefusingLane() {
        beginTest("A selection either side of a refusing lane keeps its spread");

        magda::test::runWithCleanJuceState([this] {
            DragFixture f;
            // Two clips two usable lanes apart: top and middle, with the chord
            // lane between them. A delta in raw lane indices would move them by
            // different amounts once one has stepped over and the other has not.
            const ClipId upper = f.addClip(f.topTrack, 0.0);
            const ClipId lower = f.addClip(f.middleTrack, 32.0);
            SelectionManager::getInstance().selectClips({upper, lower});

            f.panel.startMultiClipDrag(upper, {100, f.centreOfLane(f.laneOf(f.topTrack))});
            f.panel.updateMultiClipDrag({100, f.centreOfLane(f.laneOf(f.middleTrack))});
            f.panel.finishMultiClipDrag();

            // One usable lane down for both: top -> middle, middle -> bottom.
            // The pair is now against the end of the stack, which is what stops
            // it, and neither clip has been pulled onto the other's lane.
            expect(trackOf(upper) == f.middleTrack, "the upper clip should move one usable lane");
            expect(trackOf(lower) == f.bottomTrack, "the lower clip should move with it");
        });
    }

    void testDragPinsWhenNoLaneAccepts() {
        beginTest("A drag with nowhere to go keeps its vertical position");

        magda::test::runWithCleanJuceState([this] {
            auto& trackManager = TrackManager::getInstance();
            TimelineController controller;
            TrackContentPanel panel;
            const TrackId media = trackManager.createTrack("Only", TrackType::Media);
            trackManager.createTrack("Chords", TrackType::Chord);
            panel.setSize(2000, 600);
            panel.setTempo(testTempoBPM);
            panel.setZoom(testZoomPixelsPerBeat);
            panel.setController(&controller);

            const ClipId clip = ClipManager::getInstance().createMidiClipBeats(
                media, 0.0, 4.0, ClipView::Arrangement);
            SelectionManager::getInstance().selectClip(clip);
            panel.beginClipDragTargets({clip}, clip);

            // Anywhere in the stack, and off the bottom of it: the only other
            // lane refuses the clip, so vertical movement has nothing to offer.
            for (int y : {0, 40, 200, 5000}) {
                const int delta = panel.clipDragSlotDelta(y);
                expect(panel.clipDragTargetTrackId(media, delta) == media,
                       "the clip should stay on its own track at y=" + juce::String(y));
            }
        });
    }

    void testFrozenLanesAreSteppedOver() {
        beginTest("A frozen lane is stepped over rather than dropped into");

        magda::test::runWithCleanJuceState([this] {
            DragFixture f;
            auto& trackManager = TrackManager::getInstance();
            if (auto* track = trackManager.getTrack(f.middleTrack))
                track->frozen = true;

            const ClipId clip = f.addClip(f.topTrack, 0.0);
            SelectionManager::getInstance().selectClip(clip);
            f.panel.beginClipDragTargets({clip}, clip);

            // Lanes 1 and 2 both refuse now — the chord track by type, the
            // middle track by being rendered to audio — so the drag reaches
            // the bottom track or stays put, never either of them.
            for (int lane = 1; lane < 4; ++lane) {
                const int delta = f.panel.clipDragSlotDelta(f.centreOfLane(lane));
                const TrackId target = f.panel.clipDragTargetTrackId(f.topTrack, delta);
                expect(target == f.topTrack || target == f.bottomTrack,
                       "a frozen or refusing lane must not be a destination, pointer on lane " +
                           juce::String(lane));
            }

            // ...and it does reach past both of them.
            const int delta = f.panel.clipDragSlotDelta(f.centreOfLane(3));
            expect(f.panel.clipDragTargetTrackId(f.topTrack, delta) == f.bottomTrack,
                   "the drag should reach the bottom track");
        });
    }

    // getTrackIndexAtY answers -1 both for a pointer below the arrangement and
    // for one inside a track's automation lanes, and a drag that reads the
    // second as the first sends the ghost to the bottom of the stack from a
    // position the user is holding halfway up it.
    void testAnAutomationLaneBelongsToItsOwnTrack() {
        beginTest("A pointer over an automation lane resolves to the track above it");

        magda::test::runWithCleanJuceState([this] {
            DragFixture f;
            f.showAutomationLaneOn(f.topTrack);

            // The automation band is whatever part of the top track's row no
            // track's own lane area claims. Found by probing rather than
            // computed, so the test does not have to know how the row is laid
            // out -- only that a gap exists inside the stack.
            const int topLane = f.laneOf(f.topTrack);
            const int rowStart = f.panel.getTrackYPosition(topLane);
            const int rowEnd = rowStart + f.panel.getTrackTotalHeight(topLane);
            int automationY = -1;
            for (int y = rowStart; y < rowEnd; ++y)
                if (f.panel.getTrackIndexAtY(y) < 0) {
                    automationY = y;
                    break;
                }

            expect(automationY >= 0,
                   "the fixture needs a visible automation lane to have a gap at all");
            expect(automationY > rowStart && automationY < rowEnd,
                   "the gap should be inside the top track's row, not past the stack");
            if (automationY < 0)
                return;

            const ClipId clip = f.addClip(f.middleTrack, 0.0);
            SelectionManager::getInstance().selectClip(clip);
            f.panel.beginClipDragTargets({clip}, clip);

            const int delta = f.panel.clipDragSlotDelta(automationY);
            expect(f.panel.clipDragTargetTrackId(f.middleTrack, delta) == f.topTrack,
                   "an automation lane belongs to the track it was opened under, "
                   "not to the bottom of the arrangement");
        });
    }
};

static ClipDragTargetsJuceTest clipDragTargetsJuceTest;
