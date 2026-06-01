#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <utility>

#include "ui/components/common/FloatingHostWindow.hpp"

namespace magda::daw::ui {

/**
 * @brief Floating, resizable window that hosts an analyzer UI popped out of a
 *        device slot.
 *
 * Shares the common FloatingHostWindow chrome (themed non-native title bar,
 * always-on-top) with the plugin editor window. Owned by the inline analyzer UI
 * via unique_ptr (mirrors FaustCodeEditorWindow), so its lifetime is tied to the
 * component tree: destroyed with the device slot, well before app/JUCE shutdown.
 * Closing hides it; reopening re-shows the same instance. Floating always-on-top
 * means clicking back into the DAW never pushes it behind (which would leave it
 * visible-but-hidden and desync the slot's toggle button).
 */
class AnalyzerWindow : public FloatingHostWindow {
  public:
    AnalyzerWindow(const juce::String& name, std::unique_ptr<juce::Component> content)
        : FloatingHostWindow(name) {
        setContentOwned(content.release(), false);
        setResizable(true, true);
        setResizeLimits(360, 200, 4000, 3000);
        centreWithSize(720, 380);
        setVisible(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
        if (onClose)
            onClose();
    }

    // Fired when the window is closed via its X button, so the owning UI can
    // un-engage its pop-out toggle.
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerWindow)
};

}  // namespace magda::daw::ui
