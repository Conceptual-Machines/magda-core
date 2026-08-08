#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace magda {

/**
 * AI Settings dialog with configuration tabs plus a consolidated models tab:
 *  - Cloud: manage cloud provider API keys
 *  - Config: preset or per-agent provider mapping (references configured providers)
 *  - Models: embedded, sample-analysis, and stem-separation models
 *  - Remote: the MCP endpoint and WebSocket API an external AI host connects to
 *  - Clients: what each remote client is allowed to do, and what they have done
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
    class CommandModelPage;
    class ModelDownloadsPage;
    class RemoteApiPage;
    class RemoteClientsPage;

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
    std::unique_ptr<CommandModelPage> commandModelPage_;
    std::unique_ptr<ModelDownloadsPage> modelDownloadsPage_;
    std::unique_ptr<RemoteApiPage> remoteApiPage_;
    std::unique_ptr<RemoteClientsPage> remoteClientsPage_;

    juce::TextButton okBtn_{"OK"};
    juce::TextButton cancelBtn_{"Cancel"};

    void loadSettings();
    void applySettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsDialog)
};

}  // namespace magda
