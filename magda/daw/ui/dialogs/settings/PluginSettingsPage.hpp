#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
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

    /** @brief Height the page lays out in; it is shown inside a scrolling Settings tab. */
    static constexpr int kPreferredHeight = 640;

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

    // Inner model for custom directories ListBox
    class DirectoryListModel : public juce::ListBoxModel {
      public:
        std::vector<std::string>* paths = nullptr;
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
    };

    // Inner model for excluded plugins TableListBox
    class ExcludedTableModel : public juce::TableListBoxModel {
      public:
        std::vector<ExcludedPlugin>* entries = nullptr;
        int getNumRows() override;
        void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height,
                                bool rowIsSelected) override;
        void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height,
                       bool rowIsSelected) override;
        juce::Component* refreshComponentForCell(
            int rowNumber, int columnId, bool isRowSelected,
            juce::Component* existingComponentToUpdate) override;
    };

    DirectoryListModel systemDirListModel_;
    DirectoryListModel dirListModel_;
    ExcludedTableModel excludedTableModel_;

    // System directories section (read-only)
    juce::Label systemDirsHeader_;
    juce::ListBox systemDirsList_;
    std::vector<std::string> systemPaths_;

    // Custom directories section
    juce::Label directoriesHeader_;
    juce::ListBox directoriesList_;
    juce::TextButton addDirButton_;
    juce::TextButton removeDirButton_;
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
    juce::TableListBox excludedTable_;
    juce::TextButton removeSelectedButton_;
    juce::TextButton resetAllButton_;
    std::vector<ExcludedPlugin> excludedPlugins_;

    int committedFormat_ = 0;
    juce::StringArray committedExclusions_;

    void updatePluginCountLabel();
    void setupSectionHeader(juce::Label& header, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSettingsPage)
};

}  // namespace magda
