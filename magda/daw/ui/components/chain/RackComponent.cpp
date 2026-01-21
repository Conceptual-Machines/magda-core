#include "RackComponent.hpp"

#include <BinaryData.h>

#include "ChainPanel.hpp"
#include "ChainRowComponent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

RackComponent::RackComponent(magda::TrackId trackId, const magda::RackInfo& rack)
    : trackId_(trackId), rackId_(rack.id) {
    // Set up base class callbacks
    setNodeName(rack.name);
    setBypassed(rack.bypassed);

    onBypassChanged = [this](bool bypassed) {
        magda::TrackManager::getInstance().setRackBypassed(trackId_, rackId_, bypassed);
    };

    onDeleteClicked = [this]() {
        magda::TrackManager::getInstance().removeRackFromTrack(trackId_, rackId_);
    };

    onLayoutChanged = [this]() { childLayoutChanged(); };

    // === HEADER EXTRA CONTROLS ===

    // MOD button (modulators toggle) - sine wave icon
    modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::sinewavebright_svg,
                                                    BinaryData::sinewavebright_svgSize);
    modButton_->setClickingTogglesState(true);
    modButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    modButton_->setActiveColor(juce::Colours::white);
    modButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    modButton_->onClick = [this]() {
        modButton_->setActive(modButton_->getToggleState());
        modPanelVisible_ = modButton_->getToggleState();
        if (onModPanelToggled)
            onModPanelToggled(modPanelVisible_);
        if (onLayoutChanged)
            onLayoutChanged();
    };
    addAndMakeVisible(*modButton_);

    // MACRO button (macros toggle) - link icon
    macroButton_ = std::make_unique<magda::SvgButton>("Macro", BinaryData::link_bright_svg,
                                                      BinaryData::link_bright_svgSize);
    macroButton_->setClickingTogglesState(true);
    macroButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    macroButton_->setActiveColor(juce::Colours::white);
    macroButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    macroButton_->onClick = [this]() {
        macroButton_->setActive(macroButton_->getToggleState());
        paramPanelVisible_ = macroButton_->getToggleState();
        if (onParamPanelToggled)
            onParamPanelToggled(paramPanelVisible_);
        if (onLayoutChanged)
            onLayoutChanged();
    };
    addAndMakeVisible(*macroButton_);

    // === CONTENT AREA SETUP ===

    // "Chains:" label
    chainsLabel_.setText("Chains:", juce::dontSendNotification);
    chainsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    chainsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    chainsLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(chainsLabel_);

    // Add chain button (in content area, next to Chains: label)
    addChainButton_.setButtonText("+");
    addChainButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    addChainButton_.setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getSecondaryTextColour());
    addChainButton_.onClick = [this]() { onAddChainClicked(); };
    addChainButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(addChainButton_);

    // Viewport for chain rows
    chainViewport_.setViewedComponent(&chainRowsContainer_, false);
    chainViewport_.setScrollBarsShown(true, false);  // Vertical only
    addAndMakeVisible(chainViewport_);

    // Create chain panel (initially hidden)
    chainPanel_ = std::make_unique<ChainPanel>();
    chainPanel_->onClose = [this]() { hideChainPanel(); };
    chainPanel_->onDeviceSelected = [this](magda::DeviceId deviceId) {
        // Forward device selection to parent
        if (onDeviceSelected) {
            onDeviceSelected(deviceId);
        }
    };
    addChildComponent(*chainPanel_);

    // Build chain rows
    updateFromRack(rack);
}

RackComponent::~RackComponent() = default;

void RackComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Chains label separator (below "Chains:" label)
    int chainsSeparatorY = contentArea.getY() + CHAINS_LABEL_HEIGHT;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(chainsSeparatorY, static_cast<float>(contentArea.getX() + 2),
                         static_cast<float>(contentArea.getRight() - 2));
}

void RackComponent::resizedContent(juce::Rectangle<int> contentArea) {
    // Calculate chain panel positioning
    juce::Rectangle<int> chainPanelArea;
    if (chainPanel_ && chainPanel_->isVisible()) {
        int contentWidth = chainPanel_->getContentWidth();
        int chainPanelWidth = contentWidth;

        // Constrain if we have an available width limit
        if (availableWidth_ > 0) {
            int baseWidth = getMinimumWidth();
            int maxChainPanelWidth = juce::jmax(300, availableWidth_ - baseWidth);
            chainPanelWidth = juce::jmin(contentWidth, maxChainPanelWidth);
        }

        chainPanelArea = contentArea.removeFromRight(chainPanelWidth);
    }

    // "Chains:" label row with [+] button next to it
    auto chainsLabelArea = contentArea.removeFromTop(CHAINS_LABEL_HEIGHT).reduced(2, 1);
    chainsLabel_.setBounds(chainsLabelArea.removeFromLeft(45));
    chainsLabelArea.removeFromLeft(2);
    addChainButton_.setBounds(chainsLabelArea.removeFromLeft(16));

    // Chain rows viewport (below separator)
    contentArea.removeFromTop(2);  // Small gap after separator
    chainViewport_.setBounds(contentArea);

    // Calculate total height for chain rows container
    int totalHeight = 0;
    for (const auto& row : chainRows_) {
        totalHeight += row->getPreferredHeight() + 2;
    }
    totalHeight = juce::jmax(totalHeight, contentArea.getHeight());

    // Set container size and layout rows inside it
    chainRowsContainer_.setSize(
        contentArea.getWidth() - (chainViewport_.isVerticalScrollBarShown() ? 8 : 0), totalHeight);
    int y = 0;
    for (auto& row : chainRows_) {
        int rowHeight = row->getPreferredHeight();
        row->setBounds(0, y, chainRowsContainer_.getWidth(), rowHeight);
        y += rowHeight + 2;
    }

    // Position chain panel if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanel_->setBounds(chainPanelArea);
    }
}

void RackComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    // MOD and MACRO buttons in header (before name)
    modButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(4);
    macroButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(4);
}

int RackComponent::getPreferredHeight() const {
    int height = HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + 8;
    for (const auto& row : chainRows_) {
        height += row->getPreferredHeight() + 2;
    }
    return juce::jmax(height, HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + MIN_CONTENT_HEIGHT);
}

int RackComponent::getPreferredWidth() const {
    int baseWidth = getMinimumWidth();

    // Add chain panel width if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        int contentWidth = chainPanel_->getContentWidth();

        if (availableWidth_ > 0) {
            // Constrain to available width
            int maxChainPanelWidth = availableWidth_ - baseWidth;
            int chainPanelWidth = juce::jmin(contentWidth, juce::jmax(300, maxChainPanelWidth));
            return baseWidth + chainPanelWidth;
        } else {
            // No limit - expand to fit content
            return baseWidth + contentWidth;
        }
    }
    return baseWidth;
}

int RackComponent::getMinimumWidth() const {
    // Base width without chain panel
    return BASE_CHAINS_LIST_WIDTH + getLeftPanelsWidth() + getRightPanelsWidth();
}

void RackComponent::setAvailableWidth(int width) {
    availableWidth_ = width;

    // Pass remaining width to chain panel after accounting for base rack width
    if (chainPanel_ && chainPanel_->isVisible()) {
        int baseWidth = getMinimumWidth();
        int maxChainPanelWidth = juce::jmax(300, width - baseWidth);
        chainPanel_->setMaxWidth(maxChainPanelWidth);
    }
}

void RackComponent::updateFromRack(const magda::RackInfo& rack) {
    setNodeName(rack.name);
    setBypassed(rack.bypassed);
    rebuildChainRows();

    // Also refresh the chain panel if it's showing a chain
    if (chainPanel_ && chainPanel_->isVisible() && selectedChainId_ != magda::INVALID_CHAIN_ID) {
        // Check if the selected chain still exists
        const auto* chain =
            magda::TrackManager::getInstance().getChain(trackId_, rackId_, selectedChainId_);
        if (chain) {
            chainPanel_->refresh();
        } else {
            // Chain was deleted, hide the panel
            hideChainPanel();
        }
    }
}

void RackComponent::rebuildChainRows() {
    const auto* rack = magda::TrackManager::getInstance().getRack(trackId_, rackId_);
    if (!rack) {
        unfocusAllComponents();
        chainRows_.clear();
        resized();
        repaint();
        return;
    }

    // Smart rebuild: preserve existing rows, only add/remove as needed
    std::vector<std::unique_ptr<ChainRowComponent>> newRows;

    for (const auto& chain : rack->chains) {
        // Check if we already have a row for this chain
        std::unique_ptr<ChainRowComponent> existingRow;
        for (auto it = chainRows_.begin(); it != chainRows_.end(); ++it) {
            if ((*it)->getChainId() == chain.id) {
                // Found existing row - preserve it and update its data
                existingRow = std::move(*it);
                chainRows_.erase(it);
                existingRow->updateFromChain(chain);
                break;
            }
        }

        if (existingRow) {
            newRows.push_back(std::move(existingRow));
        } else {
            // Create new row for new chain
            auto row = std::make_unique<ChainRowComponent>(*this, trackId_, rackId_, chain);
            row->onSelected = [this](ChainRowComponent& selectedRow) {
                onChainRowSelected(selectedRow);
            };
            // Connect mod/macro toggle callbacks to ChainPanel
            auto chainId = chain.id;
            row->onModToggled = [this, chainId](bool visible) {
                // Only affect the ChainPanel if this chain is currently selected
                if (chainPanel_ && selectedChainId_ == chainId) {
                    chainPanel_->setModPanelVisible(visible);
                }
            };
            row->onMacroToggled = [this, chainId](bool visible) {
                // Only affect the ChainPanel if this chain is currently selected
                if (chainPanel_ && selectedChainId_ == chainId) {
                    chainPanel_->setMacroPanelVisible(visible);
                }
            };
            chainRowsContainer_.addAndMakeVisible(*row);
            newRows.push_back(std::move(row));
        }
    }

    // Unfocus before destroying remaining old rows (chains that were removed)
    if (!chainRows_.empty()) {
        unfocusAllComponents();
    }

    // Move new rows to member variable (old rows are destroyed here)
    chainRows_ = std::move(newRows);

    resized();
    repaint();
}

void RackComponent::childLayoutChanged() {
    resized();
    repaint();
    // Notify parent via callback (for TrackChainContent to relayout)
    if (onLayoutChanged) {
        onLayoutChanged();
    }
}

void RackComponent::clearChainSelection() {
    for (auto& row : chainRows_) {
        row->setSelected(false);
    }
}

void RackComponent::clearDeviceSelection() {
    if (chainPanel_) {
        chainPanel_->clearDeviceSelection();
    }
}

void RackComponent::onChainRowSelected(ChainRowComponent& row) {
    // Toggle behavior: if clicking already-selected chain, collapse/hide the panel
    if (row.isSelected() && isChainPanelVisible()) {
        hideChainPanel();
        return;
    }

    // Clear all selections first
    clearChainSelection();
    // Select the clicked row
    row.setSelected(true);
    // Show chain panel within this rack
    showChainPanel(row.getChainId());
    // Notify parent (for clearing selections in other racks)
    if (onChainSelected) {
        onChainSelected(row.getTrackId(), row.getRackId(), row.getChainId());
    }
}

void RackComponent::onAddChainClicked() {
    magda::TrackManager::getInstance().addChainToRack(trackId_, rackId_);
}

void RackComponent::showChainPanel(magda::ChainId chainId) {
    selectedChainId_ = chainId;
    if (chainPanel_) {
        chainPanel_->showChain(trackId_, rackId_, chainId);
        childLayoutChanged();  // Notify parent that our width changed
    }
}

void RackComponent::hideChainPanel() {
    selectedChainId_ = magda::INVALID_CHAIN_ID;
    clearChainSelection();
    if (chainPanel_) {
        chainPanel_->clear();
        childLayoutChanged();  // Notify parent that our width changed
    }
}

bool RackComponent::isChainPanelVisible() const {
    return chainPanel_ && chainPanel_->isVisible();
}

}  // namespace magda::daw::ui
