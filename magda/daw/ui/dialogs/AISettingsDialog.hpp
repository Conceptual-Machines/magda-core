#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace magda {

/**
 * Standalone AI Settings dialog with two tabs:
 *  - Credentials: dynamic provider list with add/validate/remove
 *  - Configuration: Easy (preset) / Advanced (per-agent mapping)
 */
class AISettingsDialog : public juce::Component {
  public:
    AISettingsDialog();
    ~AISettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

  private:
    class CredentialsPage;
    class ConfigPage;

    juce::TabbedComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<CredentialsPage> credentialsPage_;
    std::unique_ptr<ConfigPage> configPage_;

    juce::TextButton okButton_{"OK"};
    juce::TextButton cancelButton_{"Cancel"};

    void loadSettings();
    void applySettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsDialog)
};

}  // namespace magda
