#include "PadChainPanel.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "audio/MagdaSamplerPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

PadChainPanel::PadChainPanel() {
    addButton_.setColour(juce::TextButton::buttonColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    addButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    addButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addButton_.onClick = [this]() {
        if (onAddDeviceClicked && currentPadIndex_ >= 0)
            onAddDeviceClicked(currentPadIndex_);
    };
    container_.addAndMakeVisible(addButton_);

    viewport_.setScrollBarsShown(false, true);
    viewport_.setViewedComponent(&container_, false);
    addAndMakeVisible(viewport_);

    // Allow drag events to pass through container to this component
    container_.setInterceptsMouseClicks(false, true);
}

PadChainPanel::~PadChainPanel() {
    addButton_.setLookAndFeel(nullptr);
}

void PadChainPanel::showPadChain(int padIndex) {
    DBG("PadChainPanel::showPadChain - setting currentPadIndex=" + juce::String(padIndex));
    currentPadIndex_ = padIndex;
    rebuildSlots();
}

void PadChainPanel::clear() {
    currentPadIndex_ = -1;
    slots_.clear();
    container_.removeAllChildren();
    repaint();
}

void PadChainPanel::refresh() {
    DBG("PadChainPanel::refresh() called, currentPadIndex=" + juce::String(currentPadIndex_));
    if (currentPadIndex_ >= 0)
        rebuildSlots();
}

std::vector<tracktion::engine::Plugin*> PadChainPanel::getCollapsedPlugins() const {
    std::vector<tracktion::engine::Plugin*> result;
    for (auto& slot : slots_) {
        if (slot->isCollapsed() && slot->getPlugin())
            result.push_back(slot->getPlugin());
    }
    return result;
}

void PadChainPanel::setCollapsedPlugins(const std::vector<tracktion::engine::Plugin*>& plugins) {
    if (plugins.empty())
        return;
    for (auto& slot : slots_) {
        if (slot->getPlugin() &&
            std::find(plugins.begin(), plugins.end(), slot->getPlugin()) != plugins.end()) {
            if (!slot->isCollapsed()) {
                // Temporarily detach callback to avoid per-slot layout cascade
                auto saved = std::move(slot->onLayoutChanged);
                slot->setCollapsed(true);
                slot->onLayoutChanged = std::move(saved);
            }
        }
    }
    // Single layout update after all collapses
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

int PadChainPanel::getContentWidth() const {
    // Must match resized() calculation: 2px left padding + slots + arrows + 2px right padding
    // plus DROP_ZONE_WIDTH (reserved outside the viewport)
    int width = 2;
    for (auto& slot : slots_) {
        if (width > 2)
            width += ARROW_WIDTH;
        width += slot->getPreferredWidth();
    }
    width += 4 + 20 + 2;  // gap + addButton + padding
    width += DROP_ZONE_WIDTH;
    return width;
}

void PadChainPanel::rebuildSlots() {
    // Preserve collapsed state across rebuild (keyed by plugin pointer)
    std::vector<tracktion::engine::Plugin*> collapsedPlugins;
    for (auto& slot : slots_) {
        if (slot->isCollapsed() && slot->getPlugin())
            collapsedPlugins.push_back(slot->getPlugin());
    }

    slots_.clear();
    container_.removeAllChildren();
    container_.addAndMakeVisible(addButton_);

    if (currentPadIndex_ < 0 || !getPluginSlots)
        return;

    auto slotInfos = getPluginSlots(currentPadIndex_);
    DBG("PadChainPanel::rebuildSlots - pad " + juce::String(currentPadIndex_) + " has " +
        juce::String((int)slotInfos.size()) +
        " plugins, collapsedPlugins=" + juce::String((int)collapsedPlugins.size()));

    for (size_t i = 0; i < slotInfos.size(); ++i) {
        auto& info = slotInfos[i];
        DBG("  Slot " + juce::String((int)i) + ": " + info.name +
            " isSampler=" + juce::String(info.isSampler ? "true" : "false") +
            " plugin=" + juce::String::toHexString((juce::pointer_sized_int)info.plugin));

        auto slot = std::make_unique<PadDeviceSlot>();

        int pluginIndex = static_cast<int>(i);

        // Wire delete callback
        slot->onDeleteClicked = [this, pluginIndex]() {
            if (onPluginRemoved)
                onPluginRemoved(currentPadIndex_, pluginIndex);
        };

        // Wire sample operations for sampler slots
        slot->onSampleDropped = [this](const juce::File& file) {
            if (onSampleDropped)
                onSampleDropped(currentPadIndex_, file);
        };

        slot->onLoadSampleRequested = [this]() {
            if (onLoadSampleRequested)
                onLoadSampleRequested(currentPadIndex_);
        };

        slot->onLayoutChanged = [this]() {
            // Recalculate container size for the new slot widths
            resized();
            viewport_.setViewPosition(0, 0);
            repaint();
            if (onLayoutChanged)
                onLayoutChanged();
        };

        slot->onClicked = [this, pluginIndex]() {
            // Deselect other slots
            for (size_t j = 0; j < slots_.size(); ++j) {
                if (static_cast<int>(j) != pluginIndex)
                    slots_[j]->setSelected(false);
            }
            if (onDeviceClicked) {
                // Get plugin info for this slot
                juce::String name, type;
                if (pluginIndex < static_cast<int>(slots_.size())) {
                    auto slotInfos = getPluginSlots ? getPluginSlots(currentPadIndex_)
                                                    : std::vector<PluginSlotInfo>{};
                    if (pluginIndex < static_cast<int>(slotInfos.size())) {
                        auto* plugin = slotInfos[static_cast<size_t>(pluginIndex)].plugin;
                        if (plugin) {
                            name = plugin->getName();
                            if (auto* ext =
                                    dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin))
                                type = ext->desc.pluginFormatName + " Plugin";
                            else
                                type = "Internal Plugin";
                        }
                    }
                }
                onDeviceClicked(name, type);
            }
        };

        // Set plugin content
        if (info.isSampler) {
            DBG("    Setting up as sampler");
            slot->setSampler(dynamic_cast<daw::audio::MagdaSamplerPlugin*>(info.plugin));
        } else if (info.plugin) {
            DBG("    Setting up as external plugin");
            slot->setPlugin(info.plugin);
        }

        // Restore collapsed state from before rebuild
        if (info.plugin && std::find(collapsedPlugins.begin(), collapsedPlugins.end(),
                                     info.plugin) != collapsedPlugins.end()) {
            DBG("    Restoring collapsed state for " + info.name);
            slot->setCollapsed(true);
        }

        container_.addAndMakeVisible(*slot);
        slots_.push_back(std::move(slot));
    }

    resized();

    // Scroll to show the last slot (most recently added plugin)
    if (!slots_.empty()) {
        auto& lastSlot = slots_.back();
        viewport_.setViewPosition(lastSlot->getRight() - viewport_.getWidth(), 0);
    }

    repaint();

    if (onLayoutChanged)
        onLayoutChanged();
}

// =============================================================================
// DragAndDropTarget
// =============================================================================

bool PadChainPanel::isInterestedInDragSource(const SourceDetails& details) {
    if (currentPadIndex_ < 0) {
        DBG("PadChainPanel::isInterestedInDragSource - NO (currentPadIndex_ < 0)");
        return false;
    }
    if (auto* obj = details.description.getDynamicObject()) {
        bool interested = obj->getProperty("type").toString() == "plugin";
        DBG("PadChainPanel::isInterestedInDragSource - " + juce::String(interested ? "YES" : "NO") +
            " type=" + obj->getProperty("type").toString() +
            " padIndex=" + juce::String(currentPadIndex_));
        return interested;
    }
    DBG("PadChainPanel::isInterestedInDragSource - NO (no DynamicObject)");
    return false;
}

void PadChainPanel::itemDragEnter(const SourceDetails& details) {
    DBG("PadChainPanel::itemDragEnter - x=" + juce::String(details.localPosition.getX()));
    dropInsertIndex_ = calculateInsertIndex(details.localPosition.getX());
    repaint();
}

void PadChainPanel::itemDragMove(const SourceDetails& details) {
    int newIdx = calculateInsertIndex(details.localPosition.getX());
    if (newIdx != dropInsertIndex_) {
        dropInsertIndex_ = newIdx;
        repaint();
    }
}

void PadChainPanel::itemDragExit(const SourceDetails&) {
    DBG("PadChainPanel::itemDragExit");
    dropInsertIndex_ = -1;
    repaint();
}

void PadChainPanel::itemDropped(const SourceDetails& details) {
    int insertIdx = dropInsertIndex_;
    dropInsertIndex_ = -1;

    DBG("PadChainPanel::itemDropped - padIndex=" + juce::String(currentPadIndex_) +
        " insertIdx=" + juce::String(insertIdx) +
        " onPluginDropped=" + juce::String(onPluginDropped ? "SET" : "NULL") +
        " numSlotsBefore=" + juce::String(static_cast<int>(slots_.size())));

    if (currentPadIndex_ < 0) {
        DBG("  Invalid pad index, ignoring drop");
        repaint();
        return;
    }

    if (auto* obj = details.description.getDynamicObject()) {
        DBG("  Plugin drop: type=" + obj->getProperty("type").toString() +
            " fileOrId=" + obj->getProperty("fileOrIdentifier").toString() +
            " isExternal=" + obj->getProperty("isExternal").toString() +
            " uniqueId=" + obj->getProperty("uniqueId").toString());
        if (onPluginDropped)
            onPluginDropped(currentPadIndex_, *obj, insertIdx);
        else
            DBG("  WARNING: onPluginDropped callback is not set!");
    } else {
        DBG("  WARNING: dropped item has no DynamicObject");
    }

    DBG("  numSlotsAfter=" + juce::String(static_cast<int>(slots_.size())));
    repaint();
}

// =============================================================================
// Paint
// =============================================================================

void PadChainPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRect(getLocalBounds());

    // Draw drop insertion indicator
    if (dropInsertIndex_ >= 0) {
        int insertX = 0;
        if (dropInsertIndex_ < static_cast<int>(slots_.size())) {
            auto* slot = slots_[static_cast<size_t>(dropInsertIndex_)].get();
            auto slotBounds = container_.getLocalArea(slot, slot->getLocalBounds());
            insertX = viewport_.getX() + slotBounds.getX() - viewport_.getViewPositionX() - 2;
        } else if (!slots_.empty()) {
            auto* lastSlot = slots_.back().get();
            auto slotBounds = container_.getLocalArea(lastSlot, lastSlot->getLocalBounds());
            insertX = viewport_.getX() + slotBounds.getRight() - viewport_.getViewPositionX() +
                      ARROW_WIDTH / 2;
        }

        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRect(insertX, 4, 2, getHeight() - 8);
    }
}

void PadChainPanel::resized() {
    auto area = getLocalBounds();

    // Small drop zone on the right (visible target for plugin drops)
    area.removeFromRight(DROP_ZONE_WIDTH);

    // Viewport fills the rest
    viewport_.setBounds(area);

    // Calculate total content width to determine if scrollbar is needed
    int totalContentWidth = 2;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (i > 0)
            totalContentWidth += ARROW_WIDTH;
        totalContentWidth += slots_[i]->getPreferredWidth();
    }
    totalContentWidth += 2;

    bool needsScrollbar = totalContentWidth > area.getWidth();
    int scrollbarHeight = needsScrollbar ? viewport_.getScrollBarThickness() : 0;
    int height = area.getHeight() - scrollbarHeight;

    DBG("PadChainPanel::resized - bounds=" + getBounds().toString() + " area.w=" +
        juce::String(area.getWidth()) + " totalContentWidth=" + juce::String(totalContentWidth) +
        " needsScrollbar=" + juce::String(needsScrollbar ? "YES" : "NO") +
        " numSlots=" + juce::String((int)slots_.size()));
    for (size_t i = 0; i < slots_.size(); ++i) {
        DBG("  slot[" + juce::String((int)i) +
            "] preferredWidth=" + juce::String(slots_[i]->getPreferredWidth()) +
            " collapsed=" + juce::String(slots_[i]->isCollapsed() ? "YES" : "NO"));
    }

    int x = 2;
    int viewportWidth = area.getWidth();
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (i > 0)
            x += ARROW_WIDTH;
        int slotWidth = slots_[i]->getPreferredWidth();
        // Single slot (not collapsed): fill the viewport width exactly (no scrolling)
        if (slots_.size() == 1 && !slots_[i]->isCollapsed())
            slotWidth = juce::jmax(slotWidth, viewportWidth - 4);
        slots_[i]->setBounds(x, 0, slotWidth, height);
        x += slotWidth;
    }

    // "+" button after the last slot
    addButton_.setBounds(x + 4, (height - 20) / 2, 20, 20);

    x += 4 + 20 + 2;
    DBG("  containerWidth=" + juce::String(juce::jmax(x, area.getWidth())) +
        " x=" + juce::String(x));
    container_.setSize(juce::jmax(x, area.getWidth()), height);
}

int PadChainPanel::calculateInsertIndex(int mouseX) const {
    // Convert to container coordinates
    int containerX = mouseX + viewport_.getViewPositionX() - viewport_.getX();

    for (size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        int slotMid = slot->getX() + slot->getWidth() / 2;
        if (containerX < slotMid)
            return static_cast<int>(i);
    }
    return static_cast<int>(slots_.size());
}

}  // namespace magda::daw::ui
