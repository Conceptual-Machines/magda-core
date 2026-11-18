// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/ui/components/clips/ClipComponent.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"

/**
 * The volume handle on a selected audio clip, through ClipComponent's real press/drag/release:
 * only the handle takes a volume drag (#3340), and Shift on it drags the clip's gain instead.
 */

using namespace magda;

namespace {

constexpr TrackId testTrackId = 1;

juce::MouseEvent makeMouseEvent(juce::Component& component, juce::Point<int> position,
                                juce::Point<int> mouseDownPosition, juce::ModifierKeys mods) {
    return {juce::Desktop::getInstance().getMainMouseSource(),
            position.toFloat(),
            mods,
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

// A selected 4-beat audio clip at 40 px/beat: 160x60, waveform area y:[18,42), so at 0 dB the
// handle is the 8px square centred at x=80 on the waveform's top edge.
struct VolumeHandleFixture {
    TrackContentPanel panel;
    ClipId clipId = INVALID_CLIP_ID;
    std::unique_ptr<ClipComponent> component;
    const juce::Point<int> handle{80, 22};

    VolumeHandleFixture() {
        panel.setSize(2000, 400);
        panel.setTempo(120.0);
        panel.setZoom(40.0);

        clipId = ClipManager::getInstance().createAudioClipBeats(testTrackId, 0.0, 4.0,
                                                                 "/tmp/magda_volume_handle.wav");
        SelectionManager::getInstance().selectClip(clipId);

        component = std::make_unique<ClipComponent>(clipId, &panel);
        component->setSelected(true);
        panel.addAndMakeVisible(*component);
        component->setBounds(0, 0, 160, 60);
    }

    ~VolumeHandleFixture() {
        if (component)
            panel.removeChildComponent(component.get());
    }

    void press(juce::Point<int> at, juce::ModifierKeys mods = {}) {
        component->mouseDown(makeMouseEvent(*component, at, at, mods));
    }

    void dragAndRelease(juce::Point<int> from, int deltaY, juce::ModifierKeys mods = {}) {
        press(from, mods);
        const auto to = from.translated(0, deltaY);
        component->mouseDrag(makeMouseEvent(*component, to, from, mods));
        component->mouseUp(makeMouseEvent(*component, to, from, mods));
    }

    const ClipInfo& clip() const {
        return *ClipManager::getInstance().getClip(clipId);
    }
};

class ClipVolumeHandleJuceTest final : public juce::UnitTest {
  public:
    ClipVolumeHandleJuceTest() : juce::UnitTest("Clip volume handle", "magda") {}

    void runTest() override {
        const auto shift = juce::ModifierKeys(juce::ModifierKeys::shiftModifier);

        beginTest("Dragging the handle changes the clip's volume");
        magda::test::runWithCleanJuceState([this] {
            VolumeHandleFixture f;
            f.dragAndRelease(f.handle, 20);  // 0.5 dB per px, down = quieter
            expectWithinAbsoluteError(f.clip().volumeDB, -10.0f, 1e-4f);
            expectWithinAbsoluteError(f.clip().gainDB, 0.0f, 1e-4f);
            UndoManager::getInstance().undo();
            expectWithinAbsoluteError(f.clip().volumeDB, 0.0f, 1e-4f);
        });

        beginTest("Pressing the line away from the handle leaves the volume alone");
        magda::test::runWithCleanJuceState([this] {
            VolumeHandleFixture f;
            const juce::Point<int> onLine{130, 22};
            f.press(onLine);
            f.component->mouseDrag(
                makeMouseEvent(*f.component, onLine.translated(0, 20), onLine, {}));
            expectWithinAbsoluteError(f.clip().volumeDB, 0.0f, 1e-4f);
        });

        beginTest("Shift-dragging the handle changes the clip's gain, not its volume");
        magda::test::runWithCleanJuceState([this, shift] {
            VolumeHandleFixture f;
            f.dragAndRelease(f.handle, -10, shift);  // up = louder
            expectWithinAbsoluteError(f.clip().gainDB, 5.0f, 1e-4f);
            expectWithinAbsoluteError(f.clip().volumeDB, 0.0f, 1e-4f);

            // A second drag is its own undo step.
            f.dragAndRelease(f.handle, -10, shift);
            expectWithinAbsoluteError(f.clip().gainDB, 10.0f, 1e-4f);
            UndoManager::getInstance().undo();
            expectWithinAbsoluteError(f.clip().gainDB, 5.0f, 1e-4f);
            UndoManager::getInstance().undo();
            expectWithinAbsoluteError(f.clip().gainDB, 0.0f, 1e-4f);
        });
    }
};

ClipVolumeHandleJuceTest clipVolumeHandleJuceTest;

}  // namespace
