#include "PluginBrowserContent.hpp"

#include <BinaryData.h>

#include "../../dialogs/ParameterConfigDialog.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallComboBoxLookAndFeel.hpp"
#include "audio/ArpeggiatorPlugin.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "audio/StepSequencerPlugin.hpp"
#include "core/DeviceInfo.hpp"
#include "core/PluginAlias.hpp"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {

// =============================================================================
// PluginBrowserInfo
// =============================================================================

PluginBrowserInfo PluginBrowserInfo::fromPluginDescription(const juce::PluginDescription& desc) {
    PluginBrowserInfo info;
    info.name = desc.name;
    info.manufacturer = desc.manufacturerName;
    info.category = desc.isInstrument ? "Instrument" : "Effect";
    info.format = desc.pluginFormatName;
    info.subcategory = desc.category.isNotEmpty() ? desc.category : "Other";
    info.isExternal = true;
    info.uniqueId = desc.createIdentifierString();
    info.fileOrIdentifier = desc.fileOrIdentifier;
    info.alias = generateAlias(desc.name);
    return info;
}

PluginBrowserInfo PluginBrowserInfo::createInternal(const juce::String& name,
                                                    const juce::String& pluginId, bool isInstrument,
                                                    const juce::String& subcategory) {
    PluginBrowserInfo info;
    info.name = name;
    info.manufacturer = "MAGDA";
    info.category = isInstrument ? "Instrument" : "Effect";
    info.format = "Internal";
    if (subcategory.isNotEmpty())
        info.subcategory = subcategory;
    else
        info.subcategory = isInstrument ? "Synth" : "Utility";
    info.isExternal = false;
    info.uniqueId = pluginId;
    info.fileOrIdentifier = pluginId;
    info.alias = generateAlias(name);
    return info;
}

juce::String PluginBrowserInfo::generateAlias(const juce::String& pluginName) {
    return magda::pluginNameToAlias(pluginName);
}

//==============================================================================
// PluginTreeItem - Leaf item representing a single plugin
//==============================================================================
class PluginBrowserContent::PluginTreeItem : public juce::TreeViewItem {
  public:
    PluginTreeItem(const PluginBrowserInfo& plugin, PluginBrowserContent& owner)
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

        // Plugin type icon
        auto iconArea = bounds.removeFromLeft(18);
        auto iconBounds = iconArea.toFloat().reduced(1.0f);
        if (plugin_.category == "Instrument" && owner_.instrumentIcon_) {
            owner_.instrumentIcon_->drawWithin(g, iconBounds, juce::RectanglePlacement::centred,
                                               1.0f);
        } else if (plugin_.subcategory == "MIDI" && owner_.midiIcon_) {
            owner_.midiIcon_->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        } else if (owner_.effectIcon_) {
            owner_.effectIcon_->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        }
        bounds.removeFromLeft(2);

        // Format badge on the right (reserve space before drawing name)
        auto formatBounds = bounds.removeFromRight(40);

        // Plugin name
        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        auto nameBounds = bounds.reduced(4, 0);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(FontManager::getInstance().getUIFont(12.0f), plugin_.name, 0.0f, 0.0f);
        auto nameWidth =
            static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, false).getWidth()));
        g.drawText(plugin_.name, nameBounds, juce::Justification::centredLeft);

        // Alias after the name (dimmed)
        if (plugin_.alias.isNotEmpty()) {
            auto aliasBounds = nameBounds.withLeft(nameBounds.getX() + nameWidth + 6);
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText("@" + plugin_.alias, aliasBounds, juce::Justification::centredLeft);
        }
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

    // Enable drag-and-drop from plugin browser
    juce::var getDragSourceDescription() override {
        // Encode plugin info as a DynamicObject for drop targets
        auto* obj = new juce::DynamicObject();
        obj->setProperty("type", "plugin");
        obj->setProperty("name", plugin_.name);
        obj->setProperty("manufacturer", plugin_.manufacturer);
        obj->setProperty("category", plugin_.category);
        obj->setProperty("format", plugin_.format);
        obj->setProperty("subcategory", plugin_.subcategory);
        obj->setProperty("isInstrument", plugin_.category == "Instrument");
        obj->setProperty("isExternal", plugin_.isExternal);
        // External plugin identification
        obj->setProperty("uniqueId", plugin_.uniqueId);
        obj->setProperty("fileOrIdentifier", plugin_.fileOrIdentifier);
        return juce::var(obj);
    }

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override {
        return false;  // Plugin items don't accept drops
    }

  private:
    PluginBrowserInfo plugin_;
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

    instrumentIcon_ = juce::Drawable::createFromImageData(BinaryData::INSTRUMENT_DEVICE_svg,
                                                          BinaryData::INSTRUMENT_DEVICE_svgSize);
    if (instrumentIcon_)
        instrumentIcon_->replaceColour(juce::Colour(0xFFB3B3B3),
                                       DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

    effectIcon_ = juce::Drawable::createFromImageData(BinaryData::AUDIODEVICE_svg,
                                                      BinaryData::AUDIODEVICE_svgSize);
    if (effectIcon_)
        effectIcon_->replaceColour(juce::Colour(0xFFB3B3B3),
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

    midiIcon_ = juce::Drawable::createFromImageData(BinaryData::MIDIDEVICE_svg,
                                                    BinaryData::MIDIDEVICE_svgSize);
    if (midiIcon_)
        midiIcon_->replaceColour(juce::Colour(0xFFB3B3B3),
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

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
    viewModeSelector_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    viewModeSelector_.onChange = [this]() {
        currentViewMode_ = static_cast<ViewMode>(viewModeSelector_.getSelectedId() - 1);
        rebuildTree();
    };
    addAndMakeVisible(viewModeSelector_);

    // Setup tree view
    pluginTree_.setColour(juce::TreeView::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    pluginTree_.setColour(juce::TreeView::linesColourId, DarkTheme::getBorderColour());
    pluginTree_.setDefaultOpenness(false);
    pluginTree_.setMultiSelectEnabled(false);
    pluginTree_.setOpenCloseButtonsVisible(false);  // We draw our own
    addAndMakeVisible(pluginTree_);

    // Build internal plugins and tree (external plugins are loaded when engine is set)
    buildInternalPluginList();
    rebuildTree();
}

void PluginBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void PluginBrowserContent::resized() {
    auto bounds = getLocalBounds().reduced(8);

    // Top row: search box and view mode selector
    auto topRow = bounds.removeFromTop(28);
    viewModeSelector_.setBounds(topRow.removeFromRight(130));
    topRow.removeFromRight(6);
    searchBox_.setBounds(topRow);

    bounds.removeFromTop(6);

    // Tree view takes remaining space
    pluginTree_.setBounds(bounds);
}

void PluginBrowserContent::onActivated() {
    // Get engine from TrackManager if not already set
    if (!engine_) {
        if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
                TrackManager::getInstance().getAudioEngine())) {
            setEngine(engine);
        }
    }
}

void PluginBrowserContent::onDeactivated() {
    // Could save state here
}

std::vector<PluginBrowserInfo> PluginBrowserContent::getInternalPlugins() {
    std::vector<PluginBrowserInfo> list;
    list.push_back(PluginBrowserInfo::createInternal("Test Tone", "tone", false));
    list.push_back(PluginBrowserInfo::createInternal("4OSC Synth", "4osc", true));
    list.push_back(PluginBrowserInfo::createInternal(audio::MagdaSamplerPlugin::getPluginName(),
                                                     audio::MagdaSamplerPlugin::xmlTypeName, true));
    list.push_back(PluginBrowserInfo::createInternal(audio::DrumGridPlugin::getPluginName(),
                                                     audio::DrumGridPlugin::xmlTypeName, true));
    list.push_back(PluginBrowserInfo::createInternal(audio::MidiChordEnginePlugin::getPluginName(),
                                                     audio::MidiChordEnginePlugin::xmlTypeName,
                                                     false, "MIDI"));
    list.push_back(PluginBrowserInfo::createInternal(audio::ArpeggiatorPlugin::getPluginName(),
                                                     audio::ArpeggiatorPlugin::xmlTypeName, false,
                                                     "MIDI"));
    list.push_back(PluginBrowserInfo::createInternal(audio::StepSequencerPlugin::getPluginName(),
                                                     audio::StepSequencerPlugin::xmlTypeName, false,
                                                     "MIDI"));
    list.push_back(PluginBrowserInfo::createInternal("Equaliser", "eq", false, "EQ"));
    list.push_back(
        PluginBrowserInfo::createInternal("Compressor", "compressor", false, "Dynamics"));
    list.push_back(PluginBrowserInfo::createInternal("Reverb", "reverb", false, "Reverb"));
    list.push_back(PluginBrowserInfo::createInternal("Delay", "delay", false, "Delay"));
    list.push_back(PluginBrowserInfo::createInternal("Chorus", "chorus", false, "Modulation"));
    list.push_back(PluginBrowserInfo::createInternal("Phaser", "phaser", false, "Modulation"));
    list.push_back(PluginBrowserInfo::createInternal("Filter", "lowpass", false, "Filter"));
    list.push_back(PluginBrowserInfo::createInternal("Pitch Shift", "pitchshift", false, "Pitch"));
    list.push_back(
        PluginBrowserInfo::createInternal("IR Reverb", "impulseresponse", false, "Reverb"));
    list.push_back(PluginBrowserInfo::createInternal("Utility", "utility", false, "Utility"));
    return list;
}

void PluginBrowserContent::buildInternalPluginList() {
    auto internals = getInternalPlugins();
    plugins_.insert(plugins_.end(), internals.begin(), internals.end());
}

void PluginBrowserContent::loadExternalPlugins() {
    if (!engine_) {
        return;
    }

    auto& knownPlugins = engine_->getKnownPluginList();
    auto pluginTypes = knownPlugins.getTypes();

    for (const auto& desc : pluginTypes) {
        plugins_.push_back(PluginBrowserInfo::fromPluginDescription(desc));
    }

    DBG("Loaded " << pluginTypes.size() << " external plugins from KnownPluginList");
}

void PluginBrowserContent::setEngine(magda::TracktionEngineWrapper* engine) {
    // Unregister from old engine's KnownPluginList
    if (engine_) {
        engine_->getKnownPluginList().removeChangeListener(this);
    }

    engine_ = engine;

    // Register as change listener so we auto-refresh after plugin scans
    if (engine_) {
        engine_->getKnownPluginList().addChangeListener(this);
    }

    refreshPluginList();
}

PluginBrowserContent::~PluginBrowserContent() {
    if (engine_) {
        engine_->getKnownPluginList().removeChangeListener(this);
    }
    // Clear root item before TreeView destructor runs
    pluginTree_.setRootItem(nullptr);
}

void PluginBrowserContent::changeListenerCallback(juce::ChangeBroadcaster* /*source*/) {
    // KnownPluginList changed (e.g. scan completed) — refresh the browser
    refreshPluginList();
}

void PluginBrowserContent::refreshPluginList() {
    plugins_.clear();
    buildInternalPluginList();
    loadExternalPlugins();
    loadFavorites();
    loadAliases();
    rebuildTree();
}

void PluginBrowserContent::rebuildTree() {
    pluginTree_.setRootItem(nullptr);
    rootItem_.reset();

    // Create root based on view mode
    auto root = std::make_unique<CategoryTreeItem>("Plugins");

    std::map<juce::String, CategoryTreeItem*> categories;

    for (const auto& plugin : plugins_) {
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

    for (const auto& plugin : plugins_) {
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

void PluginBrowserContent::showPluginContextMenu(const PluginBrowserInfo& plugin,
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
    menu.addItem(7, "Edit Alias...");
    menu.addSeparator();
    menu.addItem(5, plugin.isFavorite ? "Remove from Favorites" : "Add to Favorites");
    menu.addSeparator();
    menu.addItem(6, "Show in Finder", !plugin.fileOrIdentifier.isEmpty());

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({position.x, position.y, 1, 1}),
        [this, plugin](int result) {
            auto& tm = magda::TrackManager::getInstance();

            // Helper to create device info from plugin
            auto createDevice = [&plugin]() {
                magda::DeviceInfo device;
                device.name = plugin.name;
                device.manufacturer = plugin.manufacturer;
                device.pluginId = plugin.uniqueId.isEmpty() ? (plugin.name + "_" + plugin.format)
                                                            : plugin.uniqueId;
                device.isInstrument = (plugin.category == "Instrument");
                if (plugin.subcategory == "MIDI")
                    device.deviceType = magda::DeviceType::MIDI;
                else if (device.isInstrument)
                    device.deviceType = magda::DeviceType::Instrument;
                else
                    device.deviceType = magda::DeviceType::Effect;
                // External plugin identification
                device.uniqueId = plugin.uniqueId;
                device.fileOrIdentifier = plugin.fileOrIdentifier;

                if (plugin.format == "VST3") {
                    device.format = magda::PluginFormat::VST3;
                } else if (plugin.format == "AU" || plugin.format == "AudioUnit") {
                    device.format = magda::PluginFormat::AU;
                } else if (plugin.format == "VST") {
                    device.format = magda::PluginFormat::VST;
                } else if (plugin.format == "Internal") {
                    device.format = magda::PluginFormat::Internal;
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
                case 5:
                    toggleFavorite(plugin);
                    break;
                case 6: {
                    juce::File pluginFile(plugin.fileOrIdentifier);
                    if (pluginFile.exists()) {
                        pluginFile.revealToUser();
                    } else {
                        DBG("Cannot reveal plugin - not a file path: " + plugin.fileOrIdentifier);
                    }
                    break;
                }
                case 7:
                    showEditAliasDialog(plugin);
                    break;
            }
        });
}

void PluginBrowserContent::showParameterConfigDialog(const PluginBrowserInfo& plugin) {
    // If it's an external plugin with a unique ID, load real parameters
    if (!plugin.uniqueId.isEmpty()) {
        ParameterConfigDialog::showForPlugin(plugin.uniqueId, plugin.name, this);
    } else {
        // Fall back to mock data for internal plugins or plugins without IDs
        ParameterConfigDialog::show(plugin.name, this);
    }
}

void PluginBrowserContent::toggleFavorite(const PluginBrowserInfo& plugin) {
    // Find matching plugin in our list and toggle
    juce::String key = plugin.uniqueId.isNotEmpty() ? plugin.uniqueId : plugin.name;

    for (auto& p : plugins_) {
        juce::String pKey = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
        if (pKey == key) {
            p.isFavorite = !p.isFavorite;
            DBG("Toggled favorite: " + p.name + " -> " + (p.isFavorite ? "true" : "false"));
            break;
        }
    }

    saveFavorites();
    rebuildTree();
}

juce::File PluginBrowserContent::getFavoritesFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("plugin_favorites.xml");
}

void PluginBrowserContent::saveFavorites() {
    juce::XmlElement root("PluginFavorites");

    for (const auto& p : plugins_) {
        if (p.isFavorite) {
            auto* elem = root.createNewChildElement("Plugin");
            elem->setAttribute("key", p.uniqueId.isNotEmpty() ? p.uniqueId : p.name);
            elem->setAttribute("name", p.name);
        }
    }

    auto file = getFavoritesFile();
    file.getParentDirectory().createDirectory();
    root.writeTo(file);
}

void PluginBrowserContent::loadFavorites() {
    auto file = getFavoritesFile();
    if (!file.existsAsFile())
        return;

    auto xml = juce::parseXML(file);
    if (!xml)
        return;

    // Collect favorite keys
    juce::StringArray favoriteKeys;
    for (auto* elem : xml->getChildIterator()) {
        favoriteKeys.add(elem->getStringAttribute("key"));
    }

    // Apply to plugins
    for (auto& p : plugins_) {
        juce::String key = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
        p.isFavorite = favoriteKeys.contains(key);
    }
}

juce::File PluginBrowserContent::getAliasesFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("plugin_aliases.xml");
}

void PluginBrowserContent::saveAliases() {
    juce::XmlElement root("PluginAliases");

    for (const auto& p : plugins_) {
        if (p.alias.isEmpty())
            continue;

        // Only save if alias differs from auto-generated default
        auto defaultAlias = PluginBrowserInfo::generateAlias(p.name);
        if (p.alias == defaultAlias)
            continue;

        auto* elem = root.createNewChildElement("Alias");
        juce::String key = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
        elem->setAttribute("key", key);
        elem->setAttribute("alias", p.alias);
    }

    auto file = getAliasesFile();
    file.getParentDirectory().createDirectory();
    root.writeTo(file);
}

void PluginBrowserContent::loadAliases() {
    auto file = getAliasesFile();
    if (!file.existsAsFile())
        return;

    auto xml = juce::parseXML(file);
    if (!xml)
        return;

    // Build a map of key -> alias
    std::map<juce::String, juce::String> aliasMap;
    for (auto* elem : xml->getChildIterator()) {
        aliasMap[elem->getStringAttribute("key")] = elem->getStringAttribute("alias");
    }

    // Apply custom aliases over auto-generated ones
    for (auto& p : plugins_) {
        juce::String key = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
        auto it = aliasMap.find(key);
        if (it != aliasMap.end()) {
            p.alias = it->second;
        }
    }
}

void PluginBrowserContent::showEditAliasDialog(const PluginBrowserInfo& plugin) {
    auto* alertWindow =
        new juce::AlertWindow("Edit Plugin Alias", "Set a custom alias for " + plugin.name + ":",
                              juce::MessageBoxIconType::NoIcon);
    alertWindow->addTextEditor("alias", plugin.alias, "Alias:");
    alertWindow->addButton("OK", 1);
    alertWindow->addButton("Cancel", 0);
    alertWindow->addButton("Reset", 2);

    juce::String pluginKey = plugin.uniqueId.isNotEmpty() ? plugin.uniqueId : plugin.name;
    juce::String pluginName = plugin.name;

    alertWindow->enterModalState(
        true,
        juce::ModalCallbackFunction::create([this, alertWindow, pluginKey, pluginName](int result) {
            if (result == 1) {
                // OK — apply custom alias
                auto newAlias = alertWindow->getTextEditorContents("alias").trim();
                if (newAlias.isNotEmpty()) {
                    for (auto& p : plugins_) {
                        juce::String key = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
                        if (key == pluginKey) {
                            p.alias = newAlias;
                            break;
                        }
                    }
                    saveAliases();
                    rebuildTree();
                }
            } else if (result == 2) {
                // Reset to auto-generated
                for (auto& p : plugins_) {
                    juce::String key = p.uniqueId.isNotEmpty() ? p.uniqueId : p.name;
                    if (key == pluginKey) {
                        p.alias = PluginBrowserInfo::generateAlias(pluginName);
                        break;
                    }
                }
                saveAliases();
                rebuildTree();
            }
            delete alertWindow;
        }),
        true);
}

}  // namespace magda::daw::ui
