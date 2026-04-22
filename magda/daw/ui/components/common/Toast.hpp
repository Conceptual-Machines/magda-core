#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief Transient floating label that auto-hides after a set duration.
 *
 * Shows a single-line text notification with a rounded background.
 * The global host is set once by MainComponent; showGlobal() is safe
 * to call from anywhere on the message thread.
 */
class Toast : public juce::Component, private juce::Timer {
  public:
    Toast();
    ~Toast() override;

    /** Show a notification for durationMs milliseconds. */
    void show(const juce::String& text, int durationMs = 3000);

    // ========================================================================
    // Global singleton host
    // ========================================================================

    /** Register the global host Toast component (called by MainComponent). */
    static void setGlobalHost(Toast* host);

    /**
     * @brief Show a toast notification via the global host.
     *
     * No-op if no host has been set (e.g. in tests or headless mode).
     * Must be called on the message thread.
     */
    static void showGlobal(const juce::String& text, int durationMs = 3000);

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;

    juce::Label label_;
    juce::String currentText_;

    static Toast* globalHost_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Toast)
};

}  // namespace magda::daw::ui
