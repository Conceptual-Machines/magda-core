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
 * A "Pin" toggle in a thin top strip controls always-on-top: pinned it floats
 * above the main window (great on a second monitor); unpinned it behaves like a
 * normal window that the DAW can sit in front of (better on a single monitor).
 * The state is reported back via onPinnedChanged so the owner can persist it.
 */
class AnalyzerWindow : public juce::DocumentWindow {
  public:
    AnalyzerWindow(const juce::String& name, std::unique_ptr<juce::Component> content,
                   bool startPinned, std::function<void(bool)> onPinnedChanged)
        : juce::DocumentWindow(name, DarkTheme::getColour(DarkTheme::BACKGROUND),
                               juce::DocumentWindow::allButtons),
          onPinnedChanged_(std::move(onPinnedChanged)) {
        setUsingNativeTitleBar(true);

        auto holder = std::make_unique<Holder>(std::move(content), startPinned);
        holder_ = holder.get();
        holder_->pinButton.onClick = [this] {
            const bool pinned = holder_->pinButton.getToggleState();
            setAlwaysOnTop(pinned);
            if (pinned)
                toFront(true);
            if (onPinnedChanged_)
                onPinnedChanged_(pinned);
        };
        setContentOwned(holder.release(), false);

        setResizable(true, true);
        setResizeLimits(360, 200, 4000, 3000);
        centreWithSize(720, 404);
        setAlwaysOnTop(startPinned);
        setVisible(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }

  private:
    // Wraps the analyzer body with a thin top strip carrying the Pin toggle.
    struct Holder : juce::Component {
        Holder(std::unique_ptr<juce::Component> b, bool pinned) : body(std::move(b)) {
            addAndMakeVisible(*body);
            pinButton.setButtonText("Pin");
            pinButton.setClickingTogglesState(true);
            pinButton.setToggleState(pinned, juce::dontSendNotification);
            pinButton.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
            pinButton.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            pinButton.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
            pinButton.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
            addAndMakeVisible(pinButton);
        }
        void resized() override {
            auto r = getLocalBounds();
            auto top = r.removeFromTop(24);
            pinButton.setBounds(top.removeFromRight(60).reduced(3, 3));
            body->setBounds(r);
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
        }
        std::unique_ptr<juce::Component> body;
        juce::TextButton pinButton;
    };

    Holder* holder_ = nullptr;
    std::function<void(bool)> onPinnedChanged_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerWindow)
};

}  // namespace magda::daw::ui
