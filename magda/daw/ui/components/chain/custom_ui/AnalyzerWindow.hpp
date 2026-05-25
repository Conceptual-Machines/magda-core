#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <utility>

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
 *
 * A "Pin" toggle in the title bar controls always-on-top: pinned it floats above
 * the main window (great on a second monitor); unpinned it behaves like a normal
 * window that the DAW can sit in front of (better on a single monitor). The state
 * is reported back via onPinnedChanged so the owner can persist it. Uses a JUCE
 * (non-native) title bar so the Pin button can live in the bar itself.
 */
class AnalyzerWindow : public juce::DocumentWindow {
  public:
    AnalyzerWindow(const juce::String& name, std::unique_ptr<juce::Component> content,
                   bool startPinned, std::function<void(bool)> onPinnedChanged)
        : juce::DocumentWindow(name, DarkTheme::getColour(DarkTheme::BACKGROUND),
                               juce::DocumentWindow::allButtons),
          onPinnedChanged_(std::move(onPinnedChanged)) {
        setUsingNativeTitleBar(false);
        setContentOwned(content.release(), false);
        setResizable(true, true);
        setResizeLimits(360, 200, 4000, 3000);
        centreWithSize(720, 380);

        pinButton_.setClickingTogglesState(true);
        pinButton_.setToggleState(startPinned, juce::dontSendNotification);
        pinButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
        pinButton_.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        pinButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getSecondaryTextColour());
        pinButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
        pinButton_.onClick = [this] {
            const bool pinned = pinButton_.getToggleState();
            setAlwaysOnTop(pinned);
            if (pinned)
                toFront(true);
            if (onPinnedChanged_)
                onPinnedChanged_(pinned);
        };
        // Base-class call: ResizableWindow hides the reference overload to
        // discourage adding children directly, but the Pin button is exactly that.
        Component::addAndMakeVisible(pinButton_);

        setAlwaysOnTop(startPinned);
        setVisible(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }

    void resized() override {
        juce::DocumentWindow::resized();
        // Pin toggle at the left of the title bar (close/minimise sit on the right).
        const int h = getTitleBarHeight();
        pinButton_.setBounds(6, (h - 16) / 2, 40, 16);
    }

  private:
    juce::TextButton pinButton_{"Pin"};
    std::function<void(bool)> onPinnedChanged_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerWindow)
};

}  // namespace magda::daw::ui
