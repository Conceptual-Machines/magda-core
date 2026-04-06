#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

/** Standard arrow size for all page navigation buttons across the app. */
static constexpr int kNavArrowSize = 12;

/**
 * Creates a left or right ArrowButton with the standard app style.
 * direction: 0.5f = left, 0.0f = right (juce::ArrowButton convention)
 */
inline std::unique_ptr<juce::ArrowButton> makeNavArrowButton(const juce::String& name,
                                                             float direction) {
    return std::make_unique<juce::ArrowButton>(name, direction,
                                               DarkTheme::getSecondaryTextColour());
}

/**
 * Places a nav arrow button centred within a slice removed from the given area.
 * Takes (kNavArrowSize + 4) px from the specified side and centres the arrow within it.
 */
inline void placeNavArrow(juce::ArrowButton& btn, juce::Rectangle<int>& area, bool fromLeft) {
    constexpr int slot = kNavArrowSize + 4;
    auto slice = fromLeft ? area.removeFromLeft(slot) : area.removeFromRight(slot);
    btn.setBounds(slice.withSizeKeepingCentre(kNavArrowSize, kNavArrowSize));
}

/**
 * Lays out the right-side buttons in a device slot header.
 *
 * Visual order (left to right): [ui?] [multiOut?] [sc?] [slider?] [power?] [delete?]
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

    if (gainSlider != nullptr) {
        gainSlider->setBounds(area.removeFromRight(sliderWidth));
        area.removeFromRight(gap);
    }

    place(scButton, buttonSize);
    place(multiOutButton, buttonSize);
    place(uiButton, buttonSize);
}

}  // namespace magda::daw::ui
