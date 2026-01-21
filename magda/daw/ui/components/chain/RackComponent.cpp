#include "RackComponent.hpp"

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

    // Hide macro buttons when param panel is hidden
    onParamPanelToggled = [this](bool visible) {
        if (!visible) {
            for (auto& btn : macroButtons_) {
                if (btn)
                    btn->setVisible(false);
            }
        }
    };

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

    // Create chain panel (initially hidden)
    chainPanel_ = std::make_unique<ChainPanel>();
    chainPanel_->onClose = [this]() { hideChainPanel(); };
    addChildComponent(*chainPanel_);

    // === MACRO BUTTONS (for param panel) ===
    for (int i = 0; i < 4; ++i) {
        macroButtons_[i] = std::make_unique<juce::TextButton>("+");
        macroButtons_[i]->setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
        macroButtons_[i]->setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getSecondaryTextColour());
        macroButtons_[i]->onClick = [this, i]() {
            juce::PopupMenu menu;
            menu.addSectionHeader("Create Macro " + juce::String(i + 1));
            menu.addItem(1, "Link to parameter...");
            menu.addItem(2, "Create empty macro");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int result) {
                if (result == 1) {
                    // TODO: Open parameter browser to link
                    macroButtons_[i]->setButtonText("M" + juce::String(i + 1));
                } else if (result == 2) {
                    macroButtons_[i]->setButtonText("M" + juce::String(i + 1));
                }
            });
        };
        addChildComponent(*macroButtons_[i]);
    }

    // Build chain rows
    updateFromRack(rack);
}

RackComponent::~RackComponent() = default;

void RackComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Remove chain panel area if visible (don't draw wrapper there)
    if (chainPanel_ && chainPanel_->isVisible()) {
        contentArea.removeFromRight(CHAIN_PANEL_WIDTH);
    }

    // Draw indented wrapper for chains area
    auto chainsWrapper = contentArea.reduced(CHAINS_INDENT, 2);

    // Wrapper background (slightly darker)
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.1f));
    g.fillRoundedRectangle(chainsWrapper.toFloat(), 2.0f);

    // Wrapper border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(chainsWrapper.toFloat(), 2.0f, 1.0f);
}

void RackComponent::resizedContent(juce::Rectangle<int> contentArea) {
    // If chain panel is visible, split the content area
    juce::Rectangle<int> chainPanelArea;
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanelArea = contentArea.removeFromRight(CHAIN_PANEL_WIDTH);
    }

    // Indent the chains area to show it's wrapped by the rack
    auto chainsWrapper = contentArea.reduced(CHAINS_INDENT, 2);

    // "Chains:" label row with [+] button
    auto chainsLabelArea = chainsWrapper.removeFromTop(CHAINS_LABEL_HEIGHT).reduced(4, 1);
    addChainButton_.setBounds(chainsLabelArea.removeFromRight(16));
    chainsLabelArea.removeFromRight(4);
    chainsLabel_.setBounds(chainsLabelArea);

    // Chain rows (below label)
    chainsWrapper.removeFromTop(2);  // Small gap after label
    int y = chainsWrapper.getY();

    for (auto& row : chainRows_) {
        int rowHeight = row->getPreferredHeight();
        row->setBounds(chainsWrapper.getX() + 2, y, chainsWrapper.getWidth() - 4, rowHeight);
        y += rowHeight + 2;
    }

    // Position chain panel if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanel_->setBounds(chainPanelArea);
    }
}

void RackComponent::resizedHeaderExtra(juce::Rectangle<int>& /*headerArea*/) {
    // No extra header buttons for rack - the add chain button is in content area
}

int RackComponent::getPreferredHeight() const {
    int height = HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + FOOTER_HEIGHT + 8;
    for (const auto& row : chainRows_) {
        height += row->getPreferredHeight() + 2;
    }
    return juce::jmax(height,
                      HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + FOOTER_HEIGHT + MIN_CONTENT_HEIGHT);
}

int RackComponent::getPreferredWidth() const {
    int width = 220;  // Base width for chains list
    // Add side panels width (left: mods+params, right: gain)
    width += getLeftPanelsWidth() + getRightPanelsWidth();
    // Add chain panel if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        width += CHAIN_PANEL_WIDTH;
    }
    return width;
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
            addAndMakeVisible(*row);
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
    if (auto* parent = getParentComponent()) {
        parent->resized();
        parent->repaint();
    }
}

void RackComponent::clearChainSelection() {
    for (auto& row : chainRows_) {
        row->setSelected(false);
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

void RackComponent::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Override: draw "MACRO" label instead of "PRM"
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("MACRO", panelArea.removeFromTop(16), juce::Justification::centred);
}

void RackComponent::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip label
    panelArea = panelArea.reduced(2);

    // 2x2 grid of macro buttons
    int btnSize = (panelArea.getWidth() - 2) / 2;
    int row = 0, col = 0;
    for (int i = 0; i < 4; ++i) {
        int x = panelArea.getX() + col * (btnSize + 2);
        int y = panelArea.getY() + row * (btnSize + 2);
        macroButtons_[i]->setBounds(x, y, btnSize, btnSize);
        macroButtons_[i]->setVisible(true);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

}  // namespace magda::daw::ui
