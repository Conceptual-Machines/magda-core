// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_clip_drop_stack_order_juce.cpp
 * @brief A clip dragged onto another is in front of it, while dragged and once dropped (#3308).
 *
 * Replays the reported gesture on the real panel: trim a clip with a time-selection edge, then
 * drag it onto a later clip, once as a move and once as an Alt-drag copy, whose preview is
 * checked mid-drag too.
 */

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOcclusion.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/clips/ClipComponent.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"
#include "magda/daw/ui/state/TimelineEvents.hpp"

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;

/** @brief A synthetic mouse event on @p target, positions given in @p panel coordinates. */
juce::MouseEvent mouseEvent(juce::Component& panel, juce::Component& target,
                            juce::Point<int> panelPosition, juce::Point<int> panelDownPosition,
                            juce::ModifierKeys mods, bool dragged) {
    const auto local = target.getLocalPoint(&panel, panelPosition);
    const auto localDown = target.getLocalPoint(&panel, panelDownPosition);
    return {juce::Desktop::getInstance().getMainMouseSource(),
            local.toFloat(),
            mods,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &target,
            &target,
            juce::Time::getCurrentTime(),
            localDown.toFloat(),
            juce::Time::getCurrentTime(),
            1,
            dragged};
}

/** @brief Press on @p target and drag it, from @p from to @p to in panel coordinates. */
void pressAndDrag(juce::Component& panel, juce::Component& target, juce::Point<int> from,
                  juce::Point<int> to, juce::ModifierKeys mods = {}) {
    const auto pressed = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
    target.mouseDown(mouseEvent(panel, target, from, from, pressed, false));
    target.mouseDrag(mouseEvent(panel, target, to, from, pressed, true));
}

/** @brief Release a drag started with pressAndDrag. */
void release(juce::Component& panel, juce::Component& target, juce::Point<int> from,
             juce::Point<int> to, juce::ModifierKeys mods = {}) {
    target.mouseUp(mouseEvent(panel, target, to, from, mods, true));
}

/** @brief Press, drag and release on @p target, from @p from to @p to in panel coordinates. */
void dragGesture(juce::Component& panel, juce::Component& target, juce::Point<int> from,
                 juce::Point<int> to, juce::ModifierKeys mods = {}) {
    pressAndDrag(panel, target, from, to, mods);
    release(panel, target, from, to, mods);
}

/** @brief How many pixels inside @p area differ between two renders of the same size. */
int pixelsDifferingIn(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int differing = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++differing;
    return differing;
}

/**
 * @brief One lane with an 8-beat clip at 0 and a 4-beat clip at 12.
 *
 * The later clip is created second, so it starts out stacked and drawn above the earlier one.
 * The selection callback is wired the way MainView wires it.
 */
struct StackFixture {
    TimelineController controller;
    TrackContentPanel panel;
    TrackId track = INVALID_TRACK_ID;
    ClipId earlier = INVALID_CLIP_ID;
    ClipId later = INVALID_CLIP_ID;

    StackFixture() {
        track = TrackManager::getInstance().createTrack("Lane", TrackType::Media);

        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
        panel.setController(&controller);
        panel.onTimeSelectionBeatsChanged = [this](double startBeats, double endBeats,
                                                   std::set<int> trackIndices) {
            if (startBeats < 0.0 || endBeats < 0.0)
                controller.dispatch(ClearTimeSelectionEvent{});
            else
                controller.dispatch(
                    SetTimeSelectionBeatsEvent{startBeats, endBeats, std::move(trackIndices)});
        };

        auto& clipManager = ClipManager::getInstance();
        earlier = clipManager.createMidiClipBeats(track, 0.0, 8.0, ClipView::Arrangement);
        later = clipManager.createMidiClipBeats(track, 12.0, 4.0, ClipView::Arrangement);
    }

    int x(double beats) const {
        return panel.beatsToPixel(beats);
    }

    /** @brief A point in the lane's upper half, where clip operations live. */
    int upperY() const {
        return panel.getTrackYPosition(0) + panel.getTrackTotalHeight(0) / 4;
    }

    /** @brief A point in the lane's lower half, where time-selection gestures live. */
    int lowerY() const {
        return panel.getTrackYPosition(0) + panel.getTrackTotalHeight(0) * 3 / 4;
    }

    /** @brief Trim the earlier clip to 0..4 by dragging a time-selection edge in from 6. */
    void trimEarlierClip() {
        controller.dispatch(SetTimeSelectionBeatsEvent{0.0, 6.0, {0}});
        dragGesture(panel, panel, {x(6.0), lowerY()}, {x(4.0), lowerY()});
    }

    /** @brief The clip component painted last, so on top, at @p beats. */
    ClipComponent* clipComponentDrawnAt(double beats) const {
        const juce::Point<int> point{x(beats), upperY()};
        const auto& children = panel.getChildren();
        for (const auto* it = children.end(); it != children.begin();) {
            auto* clipComp = dynamic_cast<ClipComponent*>(*--it);
            if (clipComp != nullptr && clipComp->isVisible() &&
                clipComp->getBounds().contains(point))
                return clipComp;
        }
        return nullptr;
    }

    ClipId clipDrawnAt(double beats) const {
        const auto* clipComp = clipComponentDrawnAt(beats);
        return clipComp != nullptr ? clipComp->getClipId() : INVALID_CLIP_ID;
    }

    /** @brief The panel as painted: lanes, clips and overlays. */
    juce::Image render() {
        juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
        juce::Graphics g(image);
        panel.paintEntireComponent(g, false);
        return image;
    }

    std::unordered_map<ClipId, AudibleSpan> audibleSpans() const {
        std::vector<ClipInfo> laneClips;
        for (auto clipId : ClipManager::getInstance().getClipsOnTrack(track))
            if (const auto* clip = ClipManager::getInstance().getClip(clipId))
                laneClips.push_back(*clip);
        return computeAudibleSpans(laneClips);
    }
};

class ClipDropStackOrderJuceTest final : public juce::UnitTest {
  public:
    ClipDropStackOrderJuceTest() : juce::UnitTest("Clip Drop Stack Order Tests", "magda") {}

    void runTest() override {
        testTrimmedClipMovedOntoAnotherLandsInFront();
        testTrimmedClipCopiedOntoAnotherLandsInFront();
    }

  private:
    void expectDroppedClipInFront(StackFixture& f, ClipId dropped) {
        const auto* droppedClip = ClipManager::getInstance().getClip(dropped);
        const auto* laterClip = ClipManager::getInstance().getClip(f.later);
        expect(droppedClip != nullptr && laterClip != nullptr, "a clip went missing");
        if (droppedClip == nullptr || laterClip == nullptr)
            return;

        expect(std::abs(droppedClip->placement.startBeat - 12.0) < 1.0e-6,
               "dropped clip should start on the later clip, at beat 12, got " +
                   juce::String(droppedClip->placement.startBeat));
        expect(clipSitsBelow(*laterClip, *droppedClip),
               "the dropped clip should stack above the clip it landed on");

        const auto spans = f.audibleSpans();
        expect(spans.at(dropped).audible, "the dropped clip should be heard");
        expect(!spans.at(f.later).audible, "the clip it covers should be silenced");

        expect(f.clipDrawnAt(14.0) == dropped, "the dropped clip should be drawn on top");
    }

    void testTrimmedClipMovedOntoAnotherLandsInFront() {
        beginTest("A trimmed clip dragged onto another is drawn and heard in front");

        magda::test::runWithCleanJuceState([this] {
            StackFixture f;
            expect(f.clipDrawnAt(14.0) == f.later, "the later clip starts out on top");

            f.trimEarlierClip();
            const auto* trimmed = ClipManager::getInstance().getClip(f.earlier);
            expect(trimmed != nullptr && std::abs(trimmed->placement.lengthBeats - 4.0) < 1.0e-6,
                   "the time-selection edge should trim the earlier clip to 4 beats");

            // The selection is now 0..4 over the trimmed clip; drag its interior to beat 12.
            dragGesture(f.panel, f.panel, {f.x(2.0), f.lowerY()}, {f.x(14.0), f.lowerY()});

            expectDroppedClipInFront(f, f.earlier);
        });
    }

    void testTrimmedClipCopiedOntoAnotherLandsInFront() {
        beginTest("A trimmed clip Alt-dragged onto another shows and lands the copy in front");

        magda::test::runWithCleanJuceState([this] {
            StackFixture f;
            f.trimEarlierClip();
            // A colour of its own, so its preview cannot blend into the clip it covers.
            ClipManager::getInstance().setClipColour(f.earlier, juce::Colours::red);

            auto* source = f.clipComponentDrawnAt(2.0);
            const auto* target = f.clipComponentDrawnAt(14.0);
            expect(source != nullptr && source->getClipId() == f.earlier,
                   "the trimmed clip should be under the pointer");
            if (source == nullptr || target == nullptr)
                return;
            const auto targetArea = target->getBounds();

            const auto before = ClipManager::getInstance().getClipsOnTrack(f.track);
            const juce::Point<int> from{f.x(2.0), f.upperY()};
            const juce::Point<int> to{f.x(14.0), f.upperY()};
            const auto alt = juce::ModifierKeys(juce::ModifierKeys::altModifier);

            const auto notDragging = f.render();
            pressAndDrag(f.panel, *source, from, to, alt);
            const auto dragging = f.render();
            const int area = targetArea.getWidth() * targetArea.getHeight();
            expect(pixelsDifferingIn(notDragging, dragging, targetArea) > area / 2,
                   "while dragging, the copy should be drawn over the clip it is dragged onto");
            release(f.panel, *source, from, to, alt);

            ClipId copy = INVALID_CLIP_ID;
            for (auto clipId : ClipManager::getInstance().getClipsOnTrack(f.track))
                if (std::ranges::find(before, clipId) == before.end())
                    copy = clipId;
            expect(copy != INVALID_CLIP_ID, "Alt-drag should create a copy");
            if (copy == INVALID_CLIP_ID)
                return;

            expectDroppedClipInFront(f, copy);
        });
    }
};

ClipDropStackOrderJuceTest clipDropStackOrderJuceTest;

}  // namespace
