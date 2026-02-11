#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "engine/PluginExclusions.hpp"

namespace magda {

class TracktionEngineWrapper;

/**
 * Plugin Settings dialog for managing custom plugin directories
 * and the excluded plugins list.
 */
class PluginSettingsDialog : public juce::Component {
  public:
    PluginSettingsDialog(TracktionEngineWrapper* engine);
    ~PluginSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(TracktionEngineWrapper* engine, juce::Component* parent);

  private:
    void applySettings();

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
    juce::TextButton viewReportButton_;
    juce::ProgressBar scanProgressBar_;
    double scanProgress_ = -1.0;
    juce::Label scanStatusLabel_;

    // Excluded plugins section
    juce::Label excludedHeader_;
    juce::TableListBox excludedTable_;
    juce::TextButton removeSelectedButton_;
    juce::TextButton resetAllButton_;
    std::vector<ExcludedPlugin> excludedPlugins_;

    // Buttons
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;

    TracktionEngineWrapper* engine_;

    void setupSectionHeader(juce::Label& header, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSettingsDialog)
};

}  // namespace magda
