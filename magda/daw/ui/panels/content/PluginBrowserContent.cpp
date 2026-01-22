#include "PluginBrowserContent.hpp"

#include "../../dialogs/ParameterConfigDialog.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

//==============================================================================
// PluginTreeItem - Leaf item representing a single plugin
//==============================================================================
class PluginBrowserContent::PluginTreeItem : public juce::TreeViewItem {
  public:
    PluginTreeItem(const MockPluginInfo& plugin, PluginBrowserContent& owner)
        : plugin_(plugin), owner_(owner) {}

    bool mightContainSubItems() override {
        return false;
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height);

        // Highlight if selected
        if (isSelected()) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRect(bounds);
        }

        // Favorite star
        if (plugin_.isFavorite) {
            g.setColour(juce::Colours::gold);
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(juce::String::fromUTF8("★"), bounds.removeFromLeft(16),
                       juce::Justification::centred);
        } else {
            bounds.removeFromLeft(16);
        }

        // Plugin type icon: 🎹 for instruments, 🎛️ for effects
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        if (plugin_.category == "Instrument") {
            g.drawText(juce::String::fromUTF8("🎹"), bounds.removeFromLeft(18),
                       juce::Justification::centred);
        } else {
            g.drawText(juce::String::fromUTF8("🎛️"), bounds.removeFromLeft(18),
                       juce::Justification::centred);
        }
        bounds.removeFromLeft(2);

        // Plugin name
        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText(plugin_.name, bounds.reduced(4, 0), juce::Justification::centredLeft);

        // Format badge on the right
        auto formatBounds = bounds.removeFromRight(40);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText(plugin_.format, formatBounds, juce::Justification::centredRight);
    }

    void itemClicked(const juce::MouseEvent& e) override {
        if (e.mods.isRightButtonDown()) {
            owner_.showPluginContextMenu(plugin_, e.getScreenPosition());
        }
    }

    void itemDoubleClicked(const juce::MouseEvent&) override {
        // Would add plugin to selected track's FX chain
        DBG("Double-clicked plugin: " + plugin_.name);
    }

    int getItemHeight() const override {
        return 24;
    }

    juce::String getUniqueName() const override {
        return plugin_.name + "_" + plugin_.format;
    }

  private:
    MockPluginInfo plugin_;
    PluginBrowserContent& owner_;
};

//==============================================================================
// CategoryTreeItem - Folder item for grouping plugins
//==============================================================================
class PluginBrowserContent::CategoryTreeItem : public juce::TreeViewItem {
  public:
    CategoryTreeItem(const juce::String& name, const juce::String& icon = "")
        : name_(name), icon_(icon) {}

    bool mightContainSubItems() override {
        return true;
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height);

        // Highlight if selected
        if (isSelected()) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.fillRect(bounds);
        }

        // Folder icon
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        juce::String folderIcon =
            isOpen() ? juce::String::fromUTF8("▼ ") : juce::String::fromUTF8("▶ ");
        g.drawText(folderIcon, bounds.removeFromLeft(20), juce::Justification::centred);

        // Category icon if provided
        if (icon_.isNotEmpty()) {
            g.drawText(icon_, bounds.removeFromLeft(20), juce::Justification::centred);
        }

        // Category name
        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
        g.drawText(name_, bounds.reduced(4, 0), juce::Justification::centredLeft);

        // Item count
        auto countBounds = bounds.removeFromRight(40);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("(" + juce::String(getNumSubItems()) + ")", countBounds,
                   juce::Justification::centredRight);
    }

    void itemClicked(const juce::MouseEvent&) override {
        // Toggle open/closed state when clicked (since we hide JUCE's built-in buttons)
        setOpen(!isOpen());
    }

    int getItemHeight() const override {
        return 26;
    }

    juce::String getUniqueName() const override {
        return name_;
    }

  private:
    juce::String name_;
    juce::String icon_;
};

//==============================================================================
// PluginBrowserContent
//==============================================================================
PluginBrowserContent::PluginBrowserContent() {
    setName("Plugin Browser");

    // Setup search box
    searchBox_.setTextToShowWhenEmpty("Search plugins...", DarkTheme::getSecondaryTextColour());
    searchBox_.setColour(juce::TextEditor::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    searchBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    searchBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    searchBox_.onTextChange = [this]() { filterBySearch(searchBox_.getText()); };
    addAndMakeVisible(searchBox_);

    // Setup view mode selector
    viewModeSelector_.addItem("By Category", 1);
    viewModeSelector_.addItem("By Manufacturer", 2);
    viewModeSelector_.addItem("By Format", 3);
    viewModeSelector_.addItem("Favorites", 4);
    viewModeSelector_.setSelectedId(1, juce::dontSendNotification);
    viewModeSelector_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    viewModeSelector_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    viewModeSelector_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
    viewModeSelector_.onChange = [this]() {
        currentViewMode_ = static_cast<ViewMode>(viewModeSelector_.getSelectedId() - 1);
        rebuildTree();
    };
    addAndMakeVisible(viewModeSelector_);

    // Setup scan button
    scanButton_.setButtonText("Scan");
    scanButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    scanButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    scanButton_.onClick = [this]() {
        // Would trigger plugin scan
        DBG("Scan plugins clicked");
    };
    addAndMakeVisible(scanButton_);

    // Setup tree view
    pluginTree_.setColour(juce::TreeView::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    pluginTree_.setColour(juce::TreeView::linesColourId, DarkTheme::getBorderColour());
    pluginTree_.setDefaultOpenness(false);
    pluginTree_.setMultiSelectEnabled(false);
    pluginTree_.setOpenCloseButtonsVisible(false);  // We draw our own
    addAndMakeVisible(pluginTree_);

    // Build mock data and tree
    buildMockPluginList();
    rebuildTree();
}

void PluginBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void PluginBrowserContent::resized() {
    auto bounds = getLocalBounds().reduced(8);

    // Top row: view selector and scan button
    auto topRow = bounds.removeFromTop(28);
    scanButton_.setBounds(topRow.removeFromRight(50));
    topRow.removeFromRight(6);
    viewModeSelector_.setBounds(topRow);

    bounds.removeFromTop(6);

    // Search box
    searchBox_.setBounds(bounds.removeFromTop(28));

    bounds.removeFromTop(6);

    // Tree view takes remaining space
    pluginTree_.setBounds(bounds);
}

void PluginBrowserContent::onActivated() {
    // Could refresh plugin list here
}

void PluginBrowserContent::onDeactivated() {
    // Could save state here
}

void PluginBrowserContent::buildMockPluginList() {
    mockPlugins_ = {
        // Instruments
        {"Serum", "Xfer Records", "Instrument", "VST3", "Synth", true},
        {"Vital", "Matt Tytel", "Instrument", "VST3", "Synth", true},
        {"Diva", "u-he", "Instrument", "VST3", "Synth", false},
        {"Pigments", "Arturia", "Instrument", "VST3", "Synth", false},
        {"Kontakt", "Native Instruments", "Instrument", "VST3", "Sampler", true},
        {"Omnisphere", "Spectrasonics", "Instrument", "VST3", "Synth", false},
        {"Phase Plant", "Kilohearts", "Instrument", "VST3", "Synth", false},
        {"Massive X", "Native Instruments", "Instrument", "VST3", "Synth", false},

        // Effects - EQ
        {"Pro-Q 3", "FabFilter", "Effect", "VST3", "EQ", true},
        {"Kirchhoff-EQ", "Three-Body Tech", "Effect", "VST3", "EQ", false},
        {"Soothe2", "oeksound", "Effect", "VST3", "EQ", true},

        // Effects - Dynamics
        {"Pro-C 2", "FabFilter", "Effect", "VST3", "Compressor", true},
        {"Pro-L 2", "FabFilter", "Effect", "VST3", "Limiter", true},
        {"Kotelnikov", "TDR", "Effect", "VST3", "Compressor", false},
        {"Limitless", "DMG Audio", "Effect", "VST3", "Limiter", false},

        // Effects - Reverb/Delay
        {"Valhalla Room", "Valhalla DSP", "Effect", "VST3", "Reverb", true},
        {"Pro-R", "FabFilter", "Effect", "VST3", "Reverb", false},
        {"Echoboy", "Soundtoys", "Effect", "VST3", "Delay", false},
        {"Timeless 3", "FabFilter", "Effect", "VST3", "Delay", false},

        // Effects - Saturation
        {"Saturn 2", "FabFilter", "Effect", "VST3", "Saturation", false},
        {"Decapitator", "Soundtoys", "Effect", "VST3", "Saturation", false},
        {"Trash 2", "iZotope", "Effect", "VST3", "Distortion", false},

        // Effects - Modulation
        {"PhaseMistress", "Soundtoys", "Effect", "VST3", "Phaser", false},
        {"MicroShift", "Soundtoys", "Effect", "VST3", "Stereo", false},

        // AU versions of some plugins
        {"Pro-Q 3", "FabFilter", "Effect", "AU", "EQ", true},
        {"Serum", "Xfer Records", "Instrument", "AU", "Synth", true},
        {"Valhalla Room", "Valhalla DSP", "Effect", "AU", "Reverb", true},
    };
}

void PluginBrowserContent::rebuildTree() {
    pluginTree_.setRootItem(nullptr);
    rootItem_.reset();

    // Create root based on view mode
    auto root = std::make_unique<CategoryTreeItem>("Plugins");

    std::map<juce::String, CategoryTreeItem*> categories;

    for (const auto& plugin : mockPlugins_) {
        juce::String groupKey;

        switch (currentViewMode_) {
            case ViewMode::ByCategory:
                groupKey = plugin.category + "/" + plugin.subcategory;
                break;
            case ViewMode::ByManufacturer:
                groupKey = plugin.manufacturer;
                break;
            case ViewMode::ByFormat:
                groupKey = plugin.format;
                break;
            case ViewMode::Favorites:
                if (!plugin.isFavorite)
                    continue;
                groupKey = "Favorites";
                break;
        }

        // For nested categories (e.g., "Effect/EQ")
        if (currentViewMode_ == ViewMode::ByCategory) {
            auto parts = juce::StringArray::fromTokens(groupKey, "/", "");
            juce::String parentKey = parts[0];
            juce::String childKey = parts.size() > 1 ? parts[1] : "";

            // Create parent category if needed
            if (categories.find(parentKey) == categories.end()) {
                auto parentItem = new CategoryTreeItem(parentKey);
                root->addSubItem(parentItem);
                categories[parentKey] = parentItem;
            }

            // Create subcategory if needed
            if (childKey.isNotEmpty()) {
                juce::String fullKey = parentKey + "/" + childKey;
                if (categories.find(fullKey) == categories.end()) {
                    auto childItem = new CategoryTreeItem(childKey);
                    categories[parentKey]->addSubItem(childItem);
                    categories[fullKey] = childItem;
                }
                categories[fullKey]->addSubItem(new PluginTreeItem(plugin, *this));
            } else {
                categories[parentKey]->addSubItem(new PluginTreeItem(plugin, *this));
            }
        } else {
            // Single-level grouping
            if (categories.find(groupKey) == categories.end()) {
                auto item = new CategoryTreeItem(groupKey);
                root->addSubItem(item);
                categories[groupKey] = item;
            }
            categories[groupKey]->addSubItem(new PluginTreeItem(plugin, *this));
        }
    }

    rootItem_ = std::move(root);
    pluginTree_.setRootItem(rootItem_.get());
    pluginTree_.setRootItemVisible(false);

    // Open first level
    for (int i = 0; i < rootItem_->getNumSubItems(); ++i) {
        rootItem_->getSubItem(i)->setOpen(true);
    }
}

void PluginBrowserContent::filterBySearch(const juce::String& searchText) {
    // For now just rebuild - a real implementation would filter the tree
    if (searchText.isEmpty()) {
        rebuildTree();
        return;
    }

    pluginTree_.setRootItem(nullptr);
    rootItem_.reset();

    auto root = std::make_unique<CategoryTreeItem>("Search Results");

    for (const auto& plugin : mockPlugins_) {
        if (plugin.name.containsIgnoreCase(searchText) ||
            plugin.manufacturer.containsIgnoreCase(searchText) ||
            plugin.subcategory.containsIgnoreCase(searchText)) {
            root->addSubItem(new PluginTreeItem(plugin, *this));
        }
    }

    rootItem_ = std::move(root);
    pluginTree_.setRootItem(rootItem_.get());
    pluginTree_.setRootItemVisible(false);
    rootItem_->setOpen(true);
}

void PluginBrowserContent::showPluginContextMenu(const MockPluginInfo& plugin,
                                                 juce::Point<int> position) {
    juce::PopupMenu menu;

    auto& trackManager = magda::TrackManager::getInstance();
    bool hasTrack = trackManager.getSelectedTrack() != magda::INVALID_TRACK_ID;
    bool hasChain = trackManager.hasSelectedChain();

    // Only show add options when selection exists
    if (hasTrack) {
        menu.addItem(1, "Add to Selected Track");
    }
    if (hasChain) {
        menu.addItem(2, "Add to Selected Chain");
    }
    if (hasTrack || hasChain) {
        menu.addSeparator();
    }

    menu.addItem(3, "Configure Parameters...");
    menu.addItem(4, "Set Gain Stage Parameter...");
    menu.addSeparator();
    menu.addItem(5, plugin.isFavorite ? "Remove from Favorites" : "Add to Favorites");
    menu.addSeparator();
    menu.addItem(6, "Show in Finder");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({position.x, position.y, 1, 1}),
        [this, plugin](int result) {
            auto& tm = magda::TrackManager::getInstance();

            // Helper to create device info from plugin
            auto createDevice = [&plugin]() {
                magda::DeviceInfo device;
                device.name = plugin.name;
                device.manufacturer = plugin.manufacturer;
                device.pluginId = plugin.name + "_" + plugin.format;
                device.isInstrument = (plugin.category == "Instrument");
                if (plugin.format == "VST3") {
                    device.format = magda::PluginFormat::VST3;
                } else if (plugin.format == "AU") {
                    device.format = magda::PluginFormat::AU;
                } else if (plugin.format == "VST") {
                    device.format = magda::PluginFormat::VST;
                }
                return device;
            };

            switch (result) {
                case 1: {
                    // Add to selected track
                    // TODO: Make insertion position user-configurable:
                    // - Currently adds to track->devices which displays BEFORE racks
                    // - Option to add after racks (true end of signal chain)
                    // - Option to add to first chain if racks exist
                    auto selectedTrack = tm.getSelectedTrack();
                    if (selectedTrack != magda::INVALID_TRACK_ID) {
                        tm.addDeviceToTrack(selectedTrack, createDevice());
                        DBG("Added device: " + plugin.name + " to track " +
                            juce::String(selectedTrack));
                    }
                    break;
                }
                case 2: {
                    // Add to selected chain
                    if (tm.hasSelectedChain()) {
                        tm.addDeviceToChain(tm.getSelectedChainTrackId(),
                                            tm.getSelectedChainRackId(), tm.getSelectedChainId(),
                                            createDevice());
                        DBG("Added device: " + plugin.name + " to selected chain");
                    }
                    break;
                }
                case 3:
                    showParameterConfigDialog(plugin);
                    break;
                case 4:
                    DBG("Set gain stage for: " + plugin.name);
                    break;
                case 5:
                    DBG("Toggle favorite: " + plugin.name);
                    break;
                case 6:
                    DBG("Show in finder: " + plugin.name);
                    break;
            }
        });
}

void PluginBrowserContent::showParameterConfigDialog(const MockPluginInfo& plugin) {
    ParameterConfigDialog::show(plugin.name, this);
}

}  // namespace magda::daw::ui
