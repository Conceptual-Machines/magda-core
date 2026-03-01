#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

class AboutDialog : public juce::DialogWindow {
  public:
    AboutDialog();

    void closeButtonPressed() override;
    static void show();

  private:
    class ContentComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutDialog)
};

}  // namespace magda
