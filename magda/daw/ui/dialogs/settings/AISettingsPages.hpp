// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace magda {

class Config;

/**
 * @brief The AI section's pages (#3162): the Assistant's presets or per-agent
 * providers, the Providers' API keys, and the Local models downloads.
 *
 * The remote API listener and its client grants live in the Connections section
 * (#2142): they govern every remote client, not just AI ones.
 */
class AISettingsPages {
  public:
    AISettingsPages();
    ~AISettingsPages();

    juce::Component& cloudPage();
    juce::Component& configPage();
    juce::Component& modelsPage();
    /** @brief Each page's status, shown at the right of the tab row. */
    juce::Component& cloudToolbar();
    juce::Component& configToolbar();
    juce::Component& modelsToolbar();

    void load(Config& config);
    void apply(Config& config);
    /** @brief Refresh the provider lists, which the Cloud tab may have changed. */
    void configShown();
    /** @brief Pick a Models category by one of the old tab names ("Local", "Stems"...). */
    bool selectLegacyCategory(const juce::String& name);

  private:
    class CloudPage;
    class LocalPage;
    class ConfigPage;
    class SampleTaggerPage;
    class BeatTrackerPage;
    class StemSeparationPage;
    class CommandModelPage;
    class ModelDownloadsPage;

    std::unique_ptr<CloudPage> cloudPage_;
    std::unique_ptr<LocalPage> localPage_;
    std::unique_ptr<ConfigPage> configPage_;
    std::unique_ptr<SampleTaggerPage> samplePage_;
    std::unique_ptr<BeatTrackerPage> beatTrackerPage_;
    std::unique_ptr<StemSeparationPage> stemsPage_;
    std::unique_ptr<CommandModelPage> commandModelPage_;
    std::unique_ptr<ModelDownloadsPage> modelDownloadsPage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsPages)
};

}  // namespace magda
