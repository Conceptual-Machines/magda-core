#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/ui/components/clips/ClipComponent.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"

/**
 * Multi-clip resize preview (#1950)
 *
 * Drives ClipComponent's real gesture path (mouseDown / mouseDrag / mouseUp)
 * rather than re-simulating the drag arithmetic, because the bug was that the
 * preview path and the commit path had drifted apart: the commit path resized
 * the whole selection, the left-edge drag only resized the clip under the
 * cursor.
 *
 * Both edges are covered so neither can regress independently, and the
 * mouse-up case pins the rewind that lets the resize commands capture pre-drag
 * state for undo.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;
constexpr TrackId testTrackId = 1;

// Non-overlapping clips, deliberately different lengths and starts so a shared
// delta cannot be confused with a shared absolute position.
constexpr double clipAStartBeats = 0.0;
constexpr double clipALengthBeats = 4.0;
constexpr double clipBStartBeats = 8.0;
constexpr double clipBLengthBeats = 6.0;

double beatsToSeconds(double beats) {
    return beats * 60.0 / testTempoBPM;
}

juce::MouseEvent makeMouseEvent(juce::Component& component, juce::Point<int> position,
                                juce::Point<int> mouseDownPosition) {
    return {juce::Desktop::getInstance().getMainMouseSource(),
            position.toFloat(),
            juce::ModifierKeys(),
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &component,
            &component,
            juce::Time::getCurrentTime(),
            mouseDownPosition.toFloat(),
            juce::Time::getCurrentTime(),
            1,
            false};
}

struct ClipPlacement {
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;

    double endSeconds() const {
        return startSeconds + lengthSeconds;
    }
};

ClipPlacement placementOf(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return {};
    return {clip->getTimelineStart(testTempoBPM), clip->getTimelineLength(testTempoBPM)};
}

void expectNear(juce::UnitTest& test, double actual, double expected, const juce::String& label) {
    constexpr double tolerance = 1.0e-6;
    test.expect(std::abs(actual - expected) <= tolerance, label + " expected " +
                                                              juce::String(expected, 6) + ", got " +
                                                              juce::String(actual, 6));
}

// Panel plus two selected MIDI clips, with a ClipComponent for the clip that
// will be dragged. MIDI keeps the gesture on the resize edges: fade and volume
// handles are audio-only, so they cannot swallow the mouseDown.
struct ResizeGestureFixture {
    TrackContentPanel panel;
    ClipId draggedClip = INVALID_CLIP_ID;
    ClipId otherClip = INVALID_CLIP_ID;
    std::unique_ptr<ClipComponent> draggedComponent;

    ResizeGestureFixture() {
        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);

        auto& clipManager = ClipManager::getInstance();
        draggedClip = clipManager.createMidiClipBeats(testTrackId, clipAStartBeats,
                                                      clipALengthBeats, ClipView::Arrangement);
        otherClip = clipManager.createMidiClipBeats(testTrackId, clipBStartBeats, clipBLengthBeats,
                                                    ClipView::Arrangement);

        SelectionManager::getInstance().selectClips({draggedClip, otherClip});

        // Origin-aligned so component-local x equals panel x, which keeps the
        // drag delta in the events obvious.
        draggedComponent = std::make_unique<ClipComponent>(draggedClip, &panel);
        draggedComponent->setSelected(true);
        panel.addAndMakeVisible(*draggedComponent);
        draggedComponent->setBounds(
            0, 0, static_cast<int>(std::round(clipALengthBeats * testZoomPixelsPerBeat)), 60);
    }

    ~ResizeGestureFixture() {
        if (draggedComponent)
            panel.removeChildComponent(draggedComponent.get());
    }

    // One drag gesture from an edge, in whole pixels. Stops before mouseUp so
    // the assertions see the live preview state.
    void dragEdgeBy(int grabX, int deltaPixels) {
        const juce::Point<int> down{grabX, 4};
        const juce::Point<int> moved{grabX + deltaPixels, 4};
        draggedComponent->mouseDown(makeMouseEvent(*draggedComponent, down, down));
        draggedComponent->mouseDrag(makeMouseEvent(*draggedComponent, moved, down));
    }

    void releaseAt(int grabX, int deltaPixels) {
        const juce::Point<int> down{grabX, 4};
        const juce::Point<int> moved{grabX + deltaPixels, 4};
        draggedComponent->mouseUp(makeMouseEvent(*draggedComponent, moved, down));
    }

    int leftEdgeX() const {
        return 1;
    }

    int rightEdgeX() const {
        return draggedComponent->getWidth() - 1;
    }
};

class MultiClipResizePreviewJuceTest final : public juce::UnitTest {
  public:
    MultiClipResizePreviewJuceTest() : juce::UnitTest("Multi Clip Resize Preview Tests", "magda") {}

    void runTest() override {
        testLeftEdgePreviewsWholeSelection();
        testRightEdgePreviewsWholeSelection();
        testMouseUpRewindsWholeSelection();
        testSingleSelectionLeavesOthersAlone();
    }

  private:
    void testLeftEdgePreviewsWholeSelection() {
        beginTest("Left-edge drag previews every selected clip (#1950)");

        magda::test::runWithCleanJuceState([this] {
            ResizeGestureFixture f;

            // Two beats to the right: both clips shrink from the start by 2 beats
            const int deltaPixels = static_cast<int>(2.0 * testZoomPixelsPerBeat);
            f.dragEdgeBy(f.leftEdgeX(), deltaPixels);

            const auto dragged = placementOf(f.draggedClip);
            const auto other = placementOf(f.otherClip);

            expectNear(*this, dragged.startSeconds, beatsToSeconds(clipAStartBeats + 2.0),
                       "dragged clip start");
            expectNear(*this, dragged.lengthSeconds, beatsToSeconds(clipALengthBeats - 2.0),
                       "dragged clip length");

            expectNear(*this, other.startSeconds, beatsToSeconds(clipBStartBeats + 2.0),
                       "other selected clip start (this is the bug: it used to stay put)");
            expectNear(*this, other.lengthSeconds, beatsToSeconds(clipBLengthBeats - 2.0),
                       "other selected clip length");

            // Left resize is a start move: both ends stay where they were
            expectNear(*this, dragged.endSeconds(),
                       beatsToSeconds(clipAStartBeats + clipALengthBeats), "dragged clip end");
            expectNear(*this, other.endSeconds(),
                       beatsToSeconds(clipBStartBeats + clipBLengthBeats),
                       "other selected clip end");
        });
    }

    void testRightEdgePreviewsWholeSelection() {
        beginTest("Right-edge drag still previews every selected clip");

        magda::test::runWithCleanJuceState([this] {
            ResizeGestureFixture f;

            const int deltaPixels = static_cast<int>(-1.0 * testZoomPixelsPerBeat);
            f.dragEdgeBy(f.rightEdgeX(), deltaPixels);

            const auto dragged = placementOf(f.draggedClip);
            const auto other = placementOf(f.otherClip);

            expectNear(*this, dragged.startSeconds, beatsToSeconds(clipAStartBeats),
                       "dragged clip start unchanged");
            expectNear(*this, dragged.lengthSeconds, beatsToSeconds(clipALengthBeats - 1.0),
                       "dragged clip length");

            expectNear(*this, other.startSeconds, beatsToSeconds(clipBStartBeats),
                       "other selected clip start unchanged");
            expectNear(*this, other.lengthSeconds, beatsToSeconds(clipBLengthBeats - 1.0),
                       "other selected clip length");
        });
    }

    void testMouseUpRewindsWholeSelection() {
        beginTest("Mouse-up rewinds every previewed clip to its pre-drag state");

        magda::test::runWithCleanJuceState([this] {
            ResizeGestureFixture f;

            // No onClipResized callback is wired, so nothing commits: what is
            // left behind is exactly the rewind the resize commands rely on.
            const int deltaPixels = static_cast<int>(2.0 * testZoomPixelsPerBeat);
            f.dragEdgeBy(f.leftEdgeX(), deltaPixels);
            f.releaseAt(f.leftEdgeX(), deltaPixels);

            const auto dragged = placementOf(f.draggedClip);
            const auto other = placementOf(f.otherClip);

            expectNear(*this, dragged.startSeconds, beatsToSeconds(clipAStartBeats),
                       "dragged clip start restored");
            expectNear(*this, dragged.lengthSeconds, beatsToSeconds(clipALengthBeats),
                       "dragged clip length restored");
            expectNear(*this, other.startSeconds, beatsToSeconds(clipBStartBeats),
                       "other selected clip start restored");
            expectNear(*this, other.lengthSeconds, beatsToSeconds(clipBLengthBeats),
                       "other selected clip length restored");
        });
    }

    void testSingleSelectionLeavesOthersAlone() {
        beginTest("Left-edge drag on a single selection touches only that clip");

        magda::test::runWithCleanJuceState([this] {
            ResizeGestureFixture f;
            SelectionManager::getInstance().selectClip(f.draggedClip);

            const int deltaPixels = static_cast<int>(2.0 * testZoomPixelsPerBeat);
            f.dragEdgeBy(f.leftEdgeX(), deltaPixels);

            const auto dragged = placementOf(f.draggedClip);
            const auto other = placementOf(f.otherClip);

            expectNear(*this, dragged.startSeconds, beatsToSeconds(clipAStartBeats + 2.0),
                       "dragged clip start");
            expectNear(*this, other.startSeconds, beatsToSeconds(clipBStartBeats),
                       "unselected clip start untouched");
            expectNear(*this, other.lengthSeconds, beatsToSeconds(clipBLengthBeats),
                       "unselected clip length untouched");
        });
    }
};

static MultiClipResizePreviewJuceTest multiClipResizePreviewJuceTestInstance;

}  // namespace
