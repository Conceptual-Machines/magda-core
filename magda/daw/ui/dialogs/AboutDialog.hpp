#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

class AboutDialog : public juce::DialogWindow {
  public:
    /// @param engineName what the running engine calls itself, shown beside the
    ///        version only where it is not the default one (#2559).
    explicit AboutDialog(juce::String engineName = {});

    void closeButtonPressed() override;
    static void show(juce::String engineName = {});

  private:
    class ContentComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutDialog)
};

}  // namespace magda
