#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace magda {

/**
 * @brief The app's alert: a tone chip beside the title and message, buttons in a footer.
 *
 * A drop-in juce::AlertWindow. JUCE's own layout runs first; every time it places a child,
 * this window lays everything out again, so text fields and custom content added by callers
 * land in the text column. Return goes to the safe button when one of the buttons is
 * destructive.
 */
class MagdaAlertWindow : public juce::AlertWindow {
  public:
    enum class ButtonRole { Secondary, Primary, Destructive, Discard };

    MagdaAlertWindow(const juce::String& title, const juce::String& message,
                     juce::MessageBoxIconType iconType,
                     juce::Component* associatedComponent = nullptr);
    ~MagdaAlertWindow() override;

    void paintShell(juce::Graphics& g);

    void resized() override;
    void childBoundsChanged(juce::Component* child) override;
    void visibilityChanged() override;

    static ButtonRole roleOf(const juce::Button& button);
    static juce::String keyHintFor(const juce::Button& button);

  private:
    enum class Tone { None, Question, Info, Warning, Danger };

    void layoutAlert();
    void assignButtonRoles();
    Tone tone() const;
    void showBackdrop(bool shown);

    juce::MessageBoxIconType iconType_;
    juce::String message_;
    juce::Component::SafePointer<juce::Component> backdropHost_;
    std::unique_ptr<juce::Component> backdrop_;
    std::unique_ptr<juce::LookAndFeel> controlsLookAndFeel_;
    juce::TextLayout titleLayout_, messageLayout_;
    juce::Rectangle<int> chipArea_, titleArea_, messageArea_, footerArea_;
    bool laying_ = false;
};

/// Builds the alert JUCE's message boxes show; the look-and-feels' createAlertWindow call this.
juce::AlertWindow* createMagdaAlertWindow(const juce::String& title, const juce::String& message,
                                          const juce::String& button1, const juce::String& button2,
                                          const juce::String& button3,
                                          juce::MessageBoxIconType iconType, int numButtons,
                                          juce::Component* associatedComponent);

}  // namespace magda
