#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/// Applied to each floating window as it opens. The main window sets it while it lives.
using FloatingWindowHook = void (*)(juce::Component& window);

namespace detail {
inline FloatingWindowHook& floatingWindowHook() {
    static FloatingWindowHook hook = nullptr;
    return hook;
}
}  // namespace detail

/// Keeps a floating window over MAGDA's main window rather than over every app (#3084). Call last
/// in its constructor: setResizable and setUsingNativeTitleBar recreate the native window.
inline void floatOverMainWindow(juce::Component& window) {
    if (auto hook = detail::floatingWindowHook())
        hook(window);
}

/// Makes @p mainWindow the owner of floating windows, or detaches them all when nullptr.
void setFloatingWindowOwner(juce::Component* mainWindow);

/// Hides the floating windows while the main window is minimised and brings them back after.
void floatingWindowOwnerMinimised(bool minimised);

}  // namespace magda
