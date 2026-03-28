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
    juce::String alias;        // @alias for DSL/AI (e.g. "serum_2", "pro_q3")
    bool isFavorite = false;
    bool isExternal = false;  // true for VST3/AU, false for internal

    // For external plugins - used for loading
    juce::String uniqueId;          // PluginDescription::createIdentifierString()
    juce::String fileOrIdentifier;  // Path to plugin file

    // Create from PluginDescription
    static PluginBrowserInfo fromPluginDescription(const juce::PluginDescription& desc);

    // Create internal plugin entry
    static PluginBrowserInfo createInternal(const juce::String& name, const juce::String& pluginId,
                                            bool isInstrument,
                                            const juce::String& subcategory = "");

    // Generate a default alias from plugin name (lowercase, underscore-separated)
    static juce::String generateAlias(const juce::String& pluginName);
};

/**
 * @brief Plugin browser panel content
 *
 * Displays a tree view of available plugins organized by category,
 * with search functionality and right-click parameter configuration.
 */
class PluginBrowserContent : public PanelContent,
                             public juce::TreeViewItem,
                             public juce::ChangeListener {
  public:
    PluginBrowserContent();
    ~PluginBrowserContent() override;

    // ChangeListener — auto-refresh when KnownPluginList changes (e.g. after scan)
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

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

    /** Returns the canonical list of built-in internal plugins. */
    static std::vector<PluginBrowserInfo> getInternalPlugins();

    // TreeViewItem interface (for root item only)
    bool mightContainSubItems() override {
        return true;
    }

  private:
    // UI Components
    juce::TextEditor searchBox_;
    juce::TreeView pluginTree_;
    juce::ComboBox viewModeSelector_;

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

    void buildInternalPluginList();
    void loadExternalPlugins();
    void rebuildTree();
    void filterBySearch(const juce::String& searchText);

    // Context menu
    void showPluginContextMenu(const PluginBrowserInfo& plugin, juce::Point<int> position);
    void showParameterConfigDialog(const PluginBrowserInfo& plugin);

    // Favorites
    void toggleFavorite(const PluginBrowserInfo& plugin);
    void saveFavorites();
    void loadFavorites();
    juce::File getFavoritesFile() const;

    // Aliases
    void showEditAliasDialog(const PluginBrowserInfo& plugin);
    void saveAliases();
    void loadAliases();
    juce::File getAliasesFile() const;

    class PluginTreeItem;
    class CategoryTreeItem;

    std::unique_ptr<juce::TreeViewItem> rootItem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserContent)
};

}  // namespace magda::daw::ui
