#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/MainLookAndFeel.hpp"

namespace magda::daw::ui {

/**
 * @brief Shared base for MAGDA's floating popout windows.
 *
 * Used by both the analyzer scope/spectrum popout and the 3rd-party plugin
 * editor window so they are literally the same window type with the same chrome:
 * a JUCE (non-native) themed title bar, a dark background, and always-on-top.
 *
 * The non-native title bar is deliberate: it gives us full control over close
 * behaviour (native macOS close can race a window's owner during teardown) and a
 * consistent themed look across every popout. Subclasses own their content and
 * their close semantics (the analyzer hides and reuses the window; the plugin
 * editor defers to Tracktion's window teardown).
 */
class FloatingHostWindow : public juce::DocumentWindow {
  public:
    explicit FloatingHostWindow(const juce::String& name,
                                int buttons = juce::DocumentWindow::minimiseButton |
                                              juce::DocumentWindow::closeButton)
        : juce::DocumentWindow(name, DarkTheme::getColour(DarkTheme::BACKGROUND), buttons) {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(MainLookAndFeel::kTitleBarHeight);
        setAlwaysOnTop(true);
    }
};

}  // namespace magda::daw::ui
