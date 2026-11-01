// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "magda/daw/ui/components/common/TimeBendEditor.hpp"

namespace {

/// The visible header controls: everything but the field, which is far taller.
std::vector<juce::Component*> headerControls(juce::Component& editor) {
    std::vector<juce::Component*> controls;
    for (auto* child : editor.getChildren())
        if (child->isVisible() && child->getHeight() <= 20)
            controls.push_back(child);
    return controls;
}

}  // namespace

class TimeBendEditorLayoutTest : public juce::UnitTest {
  public:
    TimeBendEditorLayoutTest() : juce::UnitTest("TimeBendEditorLayout", "magda") {}

    void runTest() override {
        // 292 is a step sequencer's faceplate (SequencerFaceplate.hpp); 400 a clip inspector's.
        for (const int width : {292, 400, 700}) {
            beginTest("The header fits at " + juce::String(width) + " in both views");
            magda::daw::ui::TimeBendEditor editor;
            editor.setShowsGrooves(true);
            juce::Component accessory;
            editor.setHeaderAccessory(&accessory);
            editor.setSize(width, 200);
            expectHeaderFits(editor);

            for (auto* child : editor.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*>(child);
                    button != nullptr && button->getButtonText().startsWith("DYN")) {
                    button->onClick();
                    break;
                }
            expectHeaderFits(editor);
            editor.setHeaderAccessory(nullptr);
        }
    }

  private:
    void expectHeaderFits(juce::Component& editor) {
        const auto controls = headerControls(editor);
        expect(!controls.empty());
        for (size_t i = 0; i < controls.size(); ++i) {
            const auto bounds = controls[i]->getBounds();
            expect(bounds.getWidth() > 0, "Squeezed out: " + bounds.toString());
            expect(editor.getLocalBounds().contains(bounds), "Outside: " + bounds.toString());
            for (size_t j = i + 1; j < controls.size(); ++j)
                expect(!bounds.intersects(controls[j]->getBounds()),
                       "Overlap: " + bounds.toString() + " and " +
                           controls[j]->getBounds().toString());
        }
    }
};

static TimeBendEditorLayoutTest timeBendEditorLayoutTest;
