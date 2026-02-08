#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {
class MidiBridge;  // Forward declaration
}

namespace magda::daw::ui {

/**
 * @brief Debug dialog for adjusting runtime settings and MIDI monitoring
 */
class DebugDialog : public juce::DocumentWindow {
  public:
    DebugDialog();
    ~DebugDialog() override;

    void closeButtonPressed() override;

    // Show the dialog (creates if needed)
    static void show();
    static void hide();

    /**
     * @brief Set MidiBridge reference for MIDI monitor
     * Call from MainWindow init after bridges are created.
     */
    static void setMidiBridge(magda::MidiBridge* bridge);

  private:
    class Content;
    std::unique_ptr<Content> content_;

    static std::unique_ptr<DebugDialog> instance_;
    static magda::MidiBridge* midiBridge_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugDialog)
};

}  // namespace magda::daw::ui
