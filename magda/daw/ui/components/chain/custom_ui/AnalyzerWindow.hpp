#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

/**
 * @brief Floating, resizable window that hosts an analyzer UI popped out of a
 *        device slot.
 *
 * Owned by the inline analyzer UI via unique_ptr (mirrors FaustCodeEditorWindow),
 * so its lifetime is tied to the component tree: it is destroyed with the device
 * slot, well before app/JUCE shutdown - no static state, no shutdown ordering
 * pitfalls. Closing hides it; reopening re-shows the same instance.
 */
class AnalyzerWindow : public juce::DocumentWindow {
  public:
    AnalyzerWindow(const juce::String& name, std::unique_ptr<juce::Component> content)
        : juce::DocumentWindow(name, DarkTheme::getColour(DarkTheme::BACKGROUND),
                               juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(content.release(), false);
        setResizable(true, true);
        setResizeLimits(360, 200, 4000, 3000);
        centreWithSize(720, 380);
        // Float above the main window like a scope/meter utility window: clicking
        // back into the DAW must not push it behind (which leaves it visible-but-
        // hidden and desyncs the toggle button).
        setAlwaysOnTop(true);
        setVisible(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerWindow)
};

}  // namespace magda::daw::ui
