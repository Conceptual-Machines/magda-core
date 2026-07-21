#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace magda {

/**
 * AI Settings dialog with three tabs:
 *  - Cloud: manage cloud provider API keys
 *  - Local: embedded model configuration
 *  - Config: preset or per-agent provider mapping (references configured providers)
 */
class AISettingsDialog : public juce::Component {
  public:
    AISettingsDialog();
    ~AISettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // initialTabName selects a tab by its title (e.g. "Stems") once the
    // dialog is up; empty keeps the default tab.
    static void showDialog(juce::Component* parent, const juce::String& initialTabName = {});

  private:
    class CloudPage;
    class LocalPage;
    class ConfigPage;
    class SampleTaggerPage;
    class StemSeparationPage;

    class TabComponent : public juce::TabbedComponent {
      public:
        using juce::TabbedComponent::TabbedComponent;
        std::function<void(int)> onTabChanged;
        void currentTabChanged(int idx, const juce::String&) override {
            if (onTabChanged)
                onTabChanged(idx);
        }
    };

    TabComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<CloudPage> cloudPage_;
    std::unique_ptr<LocalPage> localPage_;
    std::unique_ptr<ConfigPage> configPage_;
    std::unique_ptr<SampleTaggerPage> samplePage_;
    std::unique_ptr<StemSeparationPage> stemsPage_;

    juce::TextButton okBtn_{"OK"};
    juce::TextButton cancelBtn_{"Cancel"};

    void loadSettings();
    void applySettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsDialog)
};

}  // namespace magda
