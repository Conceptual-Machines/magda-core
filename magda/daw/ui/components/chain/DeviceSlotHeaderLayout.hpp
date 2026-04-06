#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * Lays out the right-side buttons in a device slot header.
 *
 * Visual order (left to right): [ui?] [slider?] [sc?] [multiOut?] [power?] [delete?]
 *
 * Pass nullptr for buttons that don't apply. Invisible components are skipped.
 * Each component's visibility must be set before calling this.
 */
inline void layoutDeviceSlotHeaderRight(juce::Rectangle<int>& area, int buttonSize, int gap,
                                        juce::Component* deleteButton, juce::Component* powerButton,
                                        juce::Component* multiOutButton, juce::Component* scButton,
                                        juce::Component* gainSlider, int sliderWidth,
                                        juce::Component* uiButton) {
    auto place = [&](juce::Component* c, int size) {
        if (c == nullptr || !c->isVisible())
            return;
        c->setBounds(area.removeFromRight(size));
        area.removeFromRight(gap);
    };

    place(deleteButton, buttonSize);
    place(powerButton, buttonSize);
    place(multiOutButton, buttonSize);
    place(scButton, buttonSize);

    if (gainSlider != nullptr) {
        gainSlider->setBounds(area.removeFromRight(sliderWidth));
        area.removeFromRight(gap);
    }

    place(uiButton, buttonSize);
}

}  // namespace magda::daw::ui
