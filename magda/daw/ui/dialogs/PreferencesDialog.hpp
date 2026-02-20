#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

// Forward declarations for tab page components (defined in PreferencesDialog.cpp)
class GeneralPage;
class UIPage;
class RenderingPage;
class AIPage;
class ShortcutsPage;

/**
 * Preferences dialog for editing application configuration.
 * Organised into tabs: General, UI, Rendering, AI, Shortcuts.
 */
class PreferencesDialog : public juce::Component {
  public:
    PreferencesDialog();
    ~PreferencesDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Apply current settings to Config
    void applySettings();

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent);

  private:
    juce::TabbedComponent tabbedComponent{juce::TabbedButtonBar::TabsAtTop};

    std::unique_ptr<GeneralPage> generalPage;
    std::unique_ptr<UIPage> uiPage;
    std::unique_ptr<RenderingPage> renderingPage;
    std::unique_ptr<AIPage> aiPage;
    std::unique_ptr<ShortcutsPage> shortcutsPage;

    juce::TextButton okButton;
    juce::TextButton cancelButton;
    juce::TextButton applyButton;

    void loadCurrentSettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesDialog)
};

}  // namespace magda
