#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace magda {

class SplashScreen : public juce::DocumentWindow {
  public:
    SplashScreen();

    void closeButtonPressed() override {}

    void dismiss();
    void setStatus(const juce::String& text);

    static std::unique_ptr<SplashScreen> create();

  private:
    class ContentComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreen)
};

}  // namespace magda
