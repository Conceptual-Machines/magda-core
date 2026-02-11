#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PanelContent.hpp"

namespace magda {
class TracktionEngineWrapper;
}

namespace magda::daw::ui {

/**
 * @brief Plugin info for browser display
 * Wraps either a real PluginDescription or mock data
 */
struct PluginBrowserInfo {
    juce::String name;
    juce::String manufacturer;
    juce::String category;     // Instrument, Effect, etc.
    juce::String format;       // VST3, AU, etc.
    juce::String subcategory;  // EQ, Compressor, Synth, etc.
    bool isFavorite = false;
    bool isExternal = false;  // true for VST3/AU, false for internal

    // For external plugins - used for loading
    juce::String uniqueId;          // PluginDescription::createIdentifierString()
    juce::String fileOrIdentifier;  // Path to plugin file

    // Create from PluginDescription
    static PluginBrowserInfo fromPluginDescription(const juce::PluginDescription& desc);

    // Create internal plugin entry
    static PluginBrowserInfo createInternal(const juce::String& name, const juce::String& pluginId,
                                            bool isInstrument);
};

/**
 * @brief Plugin browser panel content
 *
 * Displays a tree view of available plugins organized by category,
 * with search functionality and right-click parameter configuration.
 */
class PluginBrowserContent : public PanelContent, public juce::TreeViewItem {
  public:
    PluginBrowserContent();
    ~PluginBrowserContent() override = default;

    PanelContentType getContentType() const override {
        return PanelContentType::PluginBrowser;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::PluginBrowser, "Plugins", "Browse and insert plugins", "Plugin"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    /**
     * @brief Set the engine for plugin scanning
     */
    void setEngine(magda::TracktionEngineWrapper* engine);

    /**
     * @brief Refresh the plugin list from the engine's KnownPluginList
     */
    void refreshPluginList();

    // TreeViewItem interface (for root item only)
    bool mightContainSubItems() override {
        return true;
    }

  private:
    // UI Components
    juce::TextEditor searchBox_;
    juce::TreeView pluginTree_;
    juce::ComboBox viewModeSelector_;
    juce::TextButton scanButton_;
    juce::TextButton clearButton_;

    // View modes
    enum class ViewMode {
        ByCategory,      // Instruments, Effects
        ByManufacturer,  // Grouped by vendor
        ByFormat,        // VST3, AU
        Favorites
    };
    ViewMode currentViewMode_ = ViewMode::ByCategory;

    // Plugin data
    std::vector<PluginBrowserInfo> plugins_;
    magda::TracktionEngineWrapper* engine_ = nullptr;  // For plugin scanning

    // Progress display during scan
    double scanProgress_ = 0.0;
    std::unique_ptr<juce::ProgressBar> scanProgressBar_;
    std::unique_ptr<juce::Label> scanPluginLabel_;
    std::unique_ptr<juce::Label> scanCountLabel_;
    bool isScanningPlugins_ = false;

    // Tree building
    void buildInternalPluginList();
    void loadExternalPlugins();
    void rebuildTree();
    void filterBySearch(const juce::String& searchText);

    // Plugin scanning
    void startPluginScan();
    void onScanProgress(float progress, const juce::String& currentPlugin);
    void onScanComplete(bool success, int numPlugins, const juce::StringArray& failedPlugins);
    void showFailedPluginsDialog(const juce::StringArray& failedPlugins);

    // Context menu
    void showPluginContextMenu(const PluginBrowserInfo& plugin, juce::Point<int> position);
    void showParameterConfigDialog(const PluginBrowserInfo& plugin);

    class PluginTreeItem;
    class CategoryTreeItem;

    std::unique_ptr<juce::TreeViewItem> rootItem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserContent)
};

}  // namespace magda::daw::ui
