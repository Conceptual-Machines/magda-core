#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace magda {

class SplashScreen : public juce::DocumentWindow {
  public:
    SplashScreen();

    void closeButtonPressed() override {}

    // The background is a per-component colour override, so it outlives the
    // look-and-feel change that repaints everything else. Re-resolve it, or a
    // window left open across a theme switch keeps the old palette.
    void lookAndFeelChanged() override;

    void dismiss();
    void setStatus(const juce::String& text);

    /// Name the engine beside the version, once it has been built (#2559).
    /// Called with what the engine calls itself, so the splash reports the one
    /// that is running rather than the one the setting currently names.
    void setEngine(const juce::String& engineName);

    static std::unique_ptr<SplashScreen> create();

  private:
    class ContentComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreen)
};

}  // namespace magda
