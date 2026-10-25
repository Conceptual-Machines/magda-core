#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "core/PluginPreferences.hpp"
#include "engine/PluginExclusions.hpp"

namespace magda {

class Config;

/**
 * The Plugins settings page: custom plugin directories, scanning
 * and the excluded plugins list.
 */
class PluginSettingsPage : public juce::Component {
  public:
    PluginSettingsPage();
    ~PluginSettingsPage() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    /** @brief The height the page lays out in at @p width. */
    int getPreferredHeight(int width);

    /** @brief The plugin count and scan actions, shown in the tab row. */
    juce::Component& getToolbar() {
        return toolbar_;
    }

    /** Returns true if a plugin scan is currently in progress. */
    bool isScanRunning() const;

    void load(const Config& config);
    /** @brief The folders and startup scan; the format and exclusions live outside Config. */
    void writeTo(Config& config) const;
    void commitOutsideConfig();
    int countChangesOutsideConfig() const;
    /** @brief Take the shown format and exclusions as the committed ones. */
    void markCommitted();

    /** @brief A scan just applied every pending edit. */
    std::function<void()> onAppliedForScan;

  private:
    void applySettings();
    void setScanningUIEnabled(bool enabled);

    /** @brief A holder that tells the page when it is resized, for the tab-row toolbar. */
    class Toolbar : public juce::Component {
      public:
        std::function<void()> onResize;
        void resized() override {
            if (onResize)
                onResize();
        }
    };

    Toolbar toolbar_;
    juce::Label foldersHeader_;
    juce::Label scanningHeader_;
    std::vector<std::string> systemPaths_;
    std::vector<juce::Rectangle<int>> rowLines_;
    std::vector<juce::Rectangle<int>> folderRows_;
    std::vector<juce::Rectangle<int>> exclusionRows_;
    std::array<juce::Rectangle<int>, 3> excludedColumns_;
    std::vector<std::unique_ptr<juce::TextButton>> folderRemoveButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> exclusionRemoveButtons_;
    juce::TextButton stopScanButton_;
    juce::Label excludedEmpty_;

    // Custom directories section
    juce::TextButton addDirButton_;
    std::vector<std::string> customPaths_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // Scan section
    juce::TextButton scanButton_;
    juce::TextButton scanNewButton_;
    juce::TextButton viewReportButton_;
    juce::ToggleButton scanOnStartupToggle_;
    // Not macOS-only: a plugin shipping as both VST3 and LV2 is the same
    // duplicate on every platform. Only the AU entry inside the selector is
    // macOS-specific.
    juce::Label formatPreferenceLabel_;
    juce::ComboBox formatPreferenceSelector_;
    juce::ProgressBar scanProgressBar_;
    double scanProgress_ = -1.0;
    juce::Label scanStatusLabel_;
    juce::Label pluginCountLabel_;

    // Excluded plugins section
    juce::Label excludedHeader_;
    juce::TextButton resetAllButton_;
    std::vector<ExcludedPlugin> excludedPlugins_;

    int committedFormat_ = 0;
    juce::StringArray committedExclusions_;

    void updatePluginCountLabel();
    int layOut(int width, bool apply);
    void layoutToolbar();
    void rebuildFolderRows();
    void rebuildExclusionRows();
    void relayout();
    void setupSectionHeader(juce::Label& header, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSettingsPage)
};

}  // namespace magda
