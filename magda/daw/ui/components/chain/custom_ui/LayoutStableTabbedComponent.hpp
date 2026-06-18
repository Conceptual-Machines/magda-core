#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief A juce::TabbedComponent that keeps the active tab stable across
 *        layout operations.
 *
 * JUCE's TabbedComponent resets to tab 0 on some setBounds/resized paths.
 * This subclass distinguishes user-initiated tab switches (tracked in
 * userTabIndex_) from layout-triggered ones (guarded by inLayout_) and
 * restores the user's tab after a `setBoundsStable()`.
 *
 * Shared by the synth custom UIs (FourOscUI, FaustInstrumentTabbedUI).
 */
class LayoutStableTabbedComponent : public juce::TabbedComponent {
  public:
    using juce::TabbedComponent::TabbedComponent;

    void currentTabChanged(int newIndex, const juce::String& /*name*/) override {
        if (!inLayout_)
            userTabIndex_ = newIndex;
    }

    void setBoundsStable(juce::Rectangle<int> bounds) {
        inLayout_ = true;
        const int saved = userTabIndex_;
        setBounds(bounds);
        inLayout_ = false;
        if (saved >= 0 && saved < getNumTabs() && getCurrentTabIndex() != saved)
            setCurrentTabIndex(saved, false);
    }

    int getUserTabIndex() const {
        return userTabIndex_;
    }

  private:
    bool inLayout_ = false;
    int userTabIndex_ = 0;
};

}  // namespace magda::daw::ui
