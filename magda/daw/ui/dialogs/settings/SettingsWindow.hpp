#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SettingsSection.hpp"

namespace magda {

class AudioEngine;

/**
 * @brief The one window for app-wide settings: a sidebar of sections, tabs inside each (#3162).
 *
 * Buffered sections wait for Apply or OK; live ones (audio devices, controllers,
 * connections) take effect as they are edited. Only one window exists at a time.
 */
class SettingsWindow : public juce::DocumentWindow {
  public:
    /** @brief Open the window on @p section, or bring it to the front there. */
    static void show(SettingsSectionId section, const juce::String& tab = {});

    /** @brief What the Shortcuts and Audio & MIDI sections act on; null clears it. */
    static void setContext(juce::ApplicationCommandManager* commandManager,
                           AudioEngine* audioEngine);

    /** @brief Close the open window, discarding pending edits. */
    static void closeIfOpen();

    ~SettingsWindow() override;
    void closeButtonPressed() override;

  private:
    class Panel;
    SettingsWindow();
    Panel* panel_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};

}  // namespace magda
