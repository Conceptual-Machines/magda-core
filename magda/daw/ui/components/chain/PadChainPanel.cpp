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
    addButton_.setTooltip("Drop a plugin here to add FX");
    addAndMakeVisible(addButton_);

    viewport_.setScrollBarsShown(false, true);
    viewport_.setViewedComponent(&container_, false);
    addAndMakeVisible(viewport_);
}

PadChainPanel::~PadChainPanel() {
    addButton_.setLookAndFeel(nullptr);
}

void PadChainPanel::showPadChain(int padIndex) {
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
    if (currentPadIndex_ >= 0)
        rebuildSlots();
}

int PadChainPanel::getContentWidth() const {
    int width = 0;
    for (auto& slot : slots_) {
        if (width > 0)
            width += ARROW_WIDTH;
        width += slot->getPreferredWidth();
    }
    width += ARROW_WIDTH + ADD_BUTTON_WIDTH;
    return juce::jmax(350, width + 12);
}

void PadChainPanel::rebuildSlots() {
    slots_.clear();
    container_.removeAllChildren();

    if (currentPadIndex_ < 0 || !getPluginSlots)
        return;

    auto slotInfos = getPluginSlots(currentPadIndex_);

    for (size_t i = 0; i < slotInfos.size(); ++i) {
        auto& info = slotInfos[i];
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
            if (onLayoutChanged)
                onLayoutChanged();
        };

        // Set plugin content
        if (info.isSampler) {
            slot->setSampler(dynamic_cast<daw::audio::MagdaSamplerPlugin*>(info.plugin));
        } else if (info.plugin) {
            slot->setPlugin(info.plugin);
        }

        container_.addAndMakeVisible(*slot);
        slots_.push_back(std::move(slot));
    }

    container_.addAndMakeVisible(addButton_);
    resized();
    repaint();
}

// =============================================================================
// DragAndDropTarget
// =============================================================================

bool PadChainPanel::isInterestedInDragSource(const SourceDetails& details) {
    if (currentPadIndex_ < 0)
        return false;
    if (auto* obj = details.description.getDynamicObject())
        return obj->getProperty("type").toString() == "plugin";
    return false;
}

void PadChainPanel::itemDragEnter(const SourceDetails& details) {
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
    dropInsertIndex_ = -1;
    repaint();
}

void PadChainPanel::itemDropped(const SourceDetails& details) {
    int insertIdx = dropInsertIndex_;
    dropInsertIndex_ = -1;

    if (currentPadIndex_ < 0) {
        repaint();
        return;
    }

    if (auto* obj = details.description.getDynamicObject()) {
        if (onPluginDropped)
            onPluginDropped(currentPadIndex_, *obj, insertIdx);
    }

    repaint();
}

// =============================================================================
// Paint
// =============================================================================

void PadChainPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
    g.fillRect(getLocalBounds());

    // Draw drop insertion indicator
    if (dropInsertIndex_ >= 0) {
        int insertX = 0;
        if (dropInsertIndex_ < static_cast<int>(slots_.size())) {
            // Find position of slot at insertIndex
            auto* slot = slots_[static_cast<size_t>(dropInsertIndex_)].get();
            // Convert from container coords to our local coords
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
    viewport_.setBounds(area);

    // Layout slots horizontally in container
    int x = 4;
    int height = area.getHeight() - 8;  // leave room for scrollbar

    for (size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        int slotWidth = slot->getPreferredWidth();

        // Draw arrow space before slot (except first)
        if (i > 0)
            x += ARROW_WIDTH;

        slot->setBounds(x, 4, slotWidth, height);
        x += slotWidth;
    }

    // Add button after last slot
    x += ARROW_WIDTH;
    addButton_.setBounds(x, (area.getHeight() - ADD_BUTTON_WIDTH) / 2, ADD_BUTTON_WIDTH,
                         ADD_BUTTON_WIDTH);
    x += ADD_BUTTON_WIDTH + 4;

    container_.setSize(x, area.getHeight());
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
