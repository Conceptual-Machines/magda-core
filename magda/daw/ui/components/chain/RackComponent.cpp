#include "RackComponent.hpp"

#include <BinaryData.h>

#include "ChainPanel.hpp"
#include "ChainRowComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

// Constructor for top-level rack (in track)
RackComponent::RackComponent(magda::TrackId trackId, const magda::RackInfo& rack)
    : rackPath_(magda::ChainNodePath::rack(trackId, rack.id)), trackId_(trackId), rackId_(rack.id) {
    onDeleteClicked = [this]() {
        magda::TrackManager::getInstance().removeRackFromTrack(trackId_, rackId_);
    };
    initializeCommon(rack);
}

// Constructor for nested rack (in chain) - with full path context
RackComponent::RackComponent(const magda::ChainNodePath& rackPath, const magda::RackInfo& rack)
    : rackPath_(rackPath), trackId_(rackPath.trackId), rackId_(rack.id) {
    DBG("RackComponent (nested) created with rackPath steps="
        << rackPath_.steps.size() << ", trackId=" << rackPath_.trackId << ", rackId=" << rack.id);
    for (size_t i = 0; i < rackPath_.steps.size(); ++i) {
        DBG("  step[" << i << "]: type=" << static_cast<int>(rackPath_.steps[i].type)
                      << ", id=" << rackPath_.steps[i].id);
    }
    onDeleteClicked = [this]() {
        DBG("RackComponent::onDeleteClicked (nested) - using path-based removal");
        magda::TrackManager::getInstance().removeRackFromChainByPath(rackPath_);
    };
    initializeCommon(rack);
}

void RackComponent::initializeCommon(const magda::RackInfo& rack) {
    // Set up base class with path for selection
    setNodePath(rackPath_);
    setNodeName(rack.name);
    setBypassed(rack.bypassed);

    onBypassChanged = [this](bool bypassed) {
        magda::TrackManager::getInstance().setRackBypassed(trackId_, rackId_, bypassed);
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
        // Side panel shows alongside collapsed strip - no need to expand
        resized();
        repaint();
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
        // Side panel shows alongside collapsed strip - no need to expand
        resized();
        repaint();
        if (onParamPanelToggled)
            onParamPanelToggled(paramPanelVisible_);
        if (onLayoutChanged)
            onLayoutChanged();
    };
    addAndMakeVisible(*macroButton_);

    // Volume slider (dB format)
    volumeSlider_.setRange(-60.0, 6.0, 0.1);
    volumeSlider_.setValue(rack.volume, juce::dontSendNotification);
    volumeSlider_.onValueChanged = [this](double db) {
        // TODO: Add TrackManager method to set rack volume
        DBG("Rack volume changed to " << db << " dB");
    };
    addAndMakeVisible(volumeSlider_);

    // === CONTENT AREA SETUP ===

    // "Chains:" label - clicks pass through for selection
    chainsLabel_.setText("Chains:", juce::dontSendNotification);
    chainsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    chainsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    chainsLabel_.setJustificationType(juce::Justification::centredLeft);
    chainsLabel_.setInterceptsMouseClicks(false, false);
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
    // Allow clicks on empty areas to pass through to parent for selection
    chainViewport_.setInterceptsMouseClicks(false, true);
    chainRowsContainer_.setInterceptsMouseClicks(false, true);
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
    // IMPORTANT: Hook into ChainPanel's layout changes to propagate size changes upward.
    // When nested racks expand, this ensures the size request propagates all the way
    // up to TrackChainContent rather than just calling resized() on this RackComponent.
    chainPanel_->onLayoutChanged = [this]() { childLayoutChanged(); };
    addChildComponent(*chainPanel_);

    // Create macro panel (initially hidden, shown when macro button is toggled)
    macroPanel_ = std::make_unique<MacroPanelComponent>();
    macroPanel_->onMacroValueChanged = [this](int macroIndex, float value) {
        magda::TrackManager::getInstance().setRackMacroValue(rackPath_, macroIndex, value);
    };
    macroPanel_->onMacroTargetChanged = [this](int macroIndex, magda::MacroTarget target) {
        magda::TrackManager::getInstance().setRackMacroTarget(rackPath_, macroIndex, target);
    };
    macroPanel_->onMacroNameChanged = [this](int macroIndex, juce::String name) {
        magda::TrackManager::getInstance().setRackMacroName(rackPath_, macroIndex, name);
    };
    macroPanel_->onMacroClicked = [this](int macroIndex) {
        // Select this macro in the SelectionManager for inspector display
        magda::SelectionManager::getInstance().selectMacro(rackPath_, macroIndex);
        DBG("Macro clicked: " << macroIndex << " on path: " << rackPath_.toString());
    };
    macroPanel_->onAddPageRequested = [this](int /*itemsToAdd*/) {
        magda::TrackManager::getInstance().addRackMacroPage(rackPath_);
    };
    macroPanel_->onRemovePageRequested = [this](int /*itemsToRemove*/) {
        magda::TrackManager::getInstance().removeRackMacroPage(rackPath_);
    };
    addChildComponent(*macroPanel_);

    // Create mods panel (initially hidden, shown when mod button is toggled)
    modsPanel_ = std::make_unique<ModsPanelComponent>();
    modsPanel_->onModAmountChanged = [this](int modIndex, float amount) {
        magda::TrackManager::getInstance().setRackModAmount(rackPath_, modIndex, amount);
    };
    modsPanel_->onModTargetChanged = [this](int modIndex, magda::ModTarget target) {
        magda::TrackManager::getInstance().setRackModTarget(rackPath_, modIndex, target);
    };
    modsPanel_->onModNameChanged = [this](int modIndex, juce::String name) {
        magda::TrackManager::getInstance().setRackModName(rackPath_, modIndex, name);
    };
    modsPanel_->onModClicked = [this](int modIndex) {
        // Select this mod in the SelectionManager for inspector display
        magda::SelectionManager::getInstance().selectMod(rackPath_, modIndex);
        // TODO: Open modulator editor panel
        DBG("Mod clicked: " << modIndex << " on path: " << rackPath_.toString());
    };
    modsPanel_->onAddPageRequested = [this](int /*itemsToAdd*/) {
        magda::TrackManager::getInstance().addRackModPage(rackPath_);
    };
    modsPanel_->onRemovePageRequested = [this](int /*itemsToRemove*/) {
        magda::TrackManager::getInstance().removeRackModPage(rackPath_);
    };
    addChildComponent(*modsPanel_);

    // Build chain rows
    updateFromRack(rack);
}

RackComponent::~RackComponent() = default;

void RackComponent::mouseDown(const juce::MouseEvent& e) {
    // Let the base class handle selection - it will call selectChainNode in mouseUp
    NodeComponent::mouseDown(e);
}

void RackComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Chains label separator (below "Chains:" label)
    int chainsSeparatorY = contentArea.getY() + CHAINS_LABEL_HEIGHT;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(chainsSeparatorY, static_cast<float>(contentArea.getX() + 2),
                         static_cast<float>(contentArea.getRight() - 2));
}

void RackComponent::resizedContent(juce::Rectangle<int> contentArea) {
    // When collapsed, hide content controls only (buttons handled by resizedCollapsed)
    // NOTE: Side panels (macro/mods) visibility is managed by resizedParamPanel/resizedModPanel
    if (collapsed_) {
        chainsLabel_.setVisible(false);
        addChainButton_.setVisible(false);
        chainViewport_.setVisible(false);
        if (chainPanel_) {
            chainPanel_->setVisible(false);
        }
        // Only hide macro panel if param panel is not visible
        if (!paramPanelVisible_ && macroPanel_) {
            macroPanel_->setVisible(false);
        }
        // Only hide mods panel if mod panel is not visible
        if (!modPanelVisible_ && modsPanel_) {
            modsPanel_->setVisible(false);
        }
        volumeSlider_.setVisible(false);
        return;
    }

    // Show content controls when expanded
    chainsLabel_.setVisible(true);
    addChainButton_.setVisible(true);
    chainViewport_.setVisible(true);
    modButton_->setVisible(true);
    macroButton_->setVisible(true);
    volumeSlider_.setVisible(true);

    // Hide macro panel if param panel is not visible
    // (resizedParamPanel will show it when visible)
    if (!paramPanelVisible_ && macroPanel_) {
        macroPanel_->setVisible(false);
    }

    // Hide mods panel if mod panel is not visible
    // (resizedModPanel will show it when visible)
    if (!modPanelVisible_ && modsPanel_) {
        modsPanel_->setVisible(false);
    }

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

        // Never consume more than available, always leave minimum for chain rows
        int minChainRowsWidth = 100;  // Minimum width for chain rows to stay visible
        int maxPanelWidth = contentArea.getWidth() - minChainRowsWidth;
        if (maxPanelWidth > 0) {
            chainPanelWidth = juce::jmin(chainPanelWidth, maxPanelWidth);
        } else {
            chainPanelWidth = 0;  // Not enough space for panel
        }

        if (chainPanelWidth > 0) {
            chainPanelArea = contentArea.removeFromRight(chainPanelWidth);
        }
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

    // Volume slider on the right side of header
    volumeSlider_.setBounds(headerArea.removeFromRight(45));
    headerArea.removeFromRight(4);
}

void RackComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    // Add mod and macro buttons vertically when collapsed
    int buttonSize = juce::jmin(16, area.getWidth() - 4);

    modButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    modButton_->setVisible(true);
    area.removeFromTop(4);

    macroButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    macroButton_->setVisible(true);
}

int RackComponent::getPreferredHeight() const {
    int height = HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + 8;
    for (const auto& row : chainRows_) {
        height += row->getPreferredHeight() + 2;
    }
    return juce::jmax(height, HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + MIN_CONTENT_HEIGHT);
}

int RackComponent::getPreferredWidth() const {
    // When collapsed, return collapsed strip width + any visible side panels
    if (collapsed_) {
        return getLeftPanelsWidth() + NodeComponent::COLLAPSED_WIDTH + getRightPanelsWidth();
    }

    int baseWidth = getMinimumWidth();

    // Add chain panel width if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        int contentWidth = chainPanel_->getContentWidth();
        DBG("RackComponent::getPreferredWidth - rackId="
            << rackId_ << " baseWidth=" << baseWidth << " chainPanelContentWidth=" << contentWidth
            << " availableWidth=" << availableWidth_);

        if (availableWidth_ > 0) {
            // Constrain to available width
            int maxChainPanelWidth = availableWidth_ - baseWidth;
            int chainPanelWidth = juce::jmin(contentWidth, juce::jmax(300, maxChainPanelWidth));
            DBG("  -> returning " << (baseWidth + chainPanelWidth) << " (constrained)");
            return baseWidth + chainPanelWidth;
        } else {
            // No limit - expand to fit content
            DBG("  -> returning " << (baseWidth + contentWidth) << " (unconstrained)");
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

    // Update macro panel if visible
    if (paramPanelVisible_ && macroPanel_) {
        updateMacroPanel();
    }

    // Update mods panel if visible
    if (modPanelVisible_ && modsPanel_) {
        updateModsPanel();
    }

    // Also refresh the chain panel if it's showing a chain
    if (chainPanel_ && chainPanel_->isVisible() && selectedChainId_ != magda::INVALID_CHAIN_ID) {
        // Check if the selected chain still exists in this rack
        bool chainExists = false;
        for (const auto& chain : rack.chains) {
            if (chain.id == selectedChainId_) {
                chainExists = true;
                break;
            }
        }

        if (chainExists) {
            chainPanel_->refresh();
        } else {
            // Chain was deleted, hide the panel
            hideChainPanel();
        }
    }
}

void RackComponent::rebuildChainRows() {
    // Use path-based lookup to support nested racks at any depth
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (!rack) {
        DBG("RackComponent::rebuildChainRows - rack not found via path!");
        unfocusAllComponents();
        chainRows_.clear();
        resized();
        repaint();
        return;
    }
    DBG("RackComponent::rebuildChainRows - found rack with " << rack->chains.size() << " chains");

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
            // Update the path in case hierarchy changed
            existingRow->setNodePath(rackPath_.withChain(chain.id));
            newRows.push_back(std::move(existingRow));
        } else {
            // Create new row for new chain
            auto row = std::make_unique<ChainRowComponent>(*this, trackId_, rackId_, chain);
            // Set the full nested path (includes parent rack/chain context)
            row->setNodePath(rackPath_.withChain(chain.id));
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
    DBG("RackComponent::onChainRowSelected - rackId="
        << rackId_ << " chainId=" << row.getChainId() << " isNested=" << (isNested() ? "yes" : "no")
        << " rowSelected=" << (row.isSelected() ? "yes" : "no")
        << " panelVisible=" << (isChainPanelVisible() ? "yes" : "no"));

    // Toggle behavior: if clicking already-selected chain, collapse/hide the panel
    if (row.isSelected() && isChainPanelVisible()) {
        DBG("  -> hiding chain panel (toggle off)");
        hideChainPanel();
        return;
    }

    // Clear all selections first
    clearChainSelection();
    // Select the clicked row
    row.setSelected(true);
    // Show chain panel within this rack
    DBG("  -> showing chain panel for chainId=" << row.getChainId());
    showChainPanel(row.getChainId());
    // Notify parent (for clearing selections in other racks)
    if (onChainSelected) {
        DBG("  -> calling onChainSelected callback (rackId=" << row.getRackId() << ")");
        onChainSelected(row.getTrackId(), row.getRackId(), row.getChainId());
    } else {
        DBG("  -> onChainSelected callback NOT set (nested rack)");
    }
}

void RackComponent::onAddChainClicked() {
    DBG("RackComponent::onAddChainClicked - rackPath_ has "
        << rackPath_.steps.size() << " steps, trackId=" << rackPath_.trackId);
    for (size_t i = 0; i < rackPath_.steps.size(); ++i) {
        DBG("  step[" << i << "]: type=" << static_cast<int>(rackPath_.steps[i].type)
                      << ", id=" << rackPath_.steps[i].id);
    }
    magda::TrackManager::getInstance().addChainToRack(rackPath_);
}

void RackComponent::showChainPanel(magda::ChainId chainId) {
    selectedChainId_ = chainId;
    if (chainPanel_) {
        auto chainPath = rackPath_.withChain(chainId);
        DBG("RackComponent::showChainPanel - rackId=" << rackId_ << " chainId=" << chainId);
        DBG("  chainPath has " << chainPath.steps.size() << " steps");
        for (size_t i = 0; i < chainPath.steps.size(); ++i) {
            DBG("  step[" << i << "]: type=" << static_cast<int>(chainPath.steps[i].type)
                          << ", id=" << chainPath.steps[i].id);
        }
        chainPanel_->showChain(chainPath);
        childLayoutChanged();
    }
}

void RackComponent::hideChainPanel() {
    DBG("RackComponent::hideChainPanel called - rackId=" << rackId_ << " isNested="
                                                         << (isNested() ? "yes" : "no"));
    // Print stack trace hint
    DBG("  (check call stack to see who called hideChainPanel)");
    selectedChainId_ = magda::INVALID_CHAIN_ID;
    clearChainSelection();
    if (chainPanel_) {
        chainPanel_->clear();
        childLayoutChanged();
    }
}

bool RackComponent::isChainPanelVisible() const {
    return chainPanel_ && chainPanel_->isVisible();
}

void RackComponent::updateMacroPanel() {
    if (!macroPanel_) {
        return;
    }

    // Get the rack data via path resolution
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (!rack) {
        return;
    }

    // Update macros
    macroPanel_->setMacros(rack->macros);

    // Collect available devices for linking (devices from all chains in this rack)
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices;
    for (const auto& chain : rack->chains) {
        for (const auto& element : chain.elements) {
            if (magda::isDevice(element)) {
                const auto& device = magda::getDevice(element);
                availableDevices.emplace_back(device.id, device.name);
            }
        }
    }
    macroPanel_->setAvailableDevices(availableDevices);
}

int RackComponent::getParamPanelWidth() const {
    // Width for 2 columns of macro knobs (2x4 grid)
    return 130;
}

void RackComponent::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw "MACROS" header
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("MACROS", panelArea.removeFromTop(16), juce::Justification::centred);
}

void RackComponent::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip "MACROS" header

    if (macroPanel_) {
        macroPanel_->setBounds(panelArea);
        macroPanel_->setVisible(true);
        updateMacroPanel();
    }
}

void RackComponent::updateModsPanel() {
    if (!modsPanel_) {
        return;
    }

    // Get the rack data via path resolution
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (!rack) {
        return;
    }

    // Update mods
    modsPanel_->setMods(rack->mods);

    // Collect available devices for linking (devices from all chains in this rack)
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices;
    for (const auto& chain : rack->chains) {
        for (const auto& element : chain.elements) {
            if (magda::isDevice(element)) {
                const auto& device = magda::getDevice(element);
                availableDevices.emplace_back(device.id, device.name);
            }
        }
    }
    modsPanel_->setAvailableDevices(availableDevices);
}

int RackComponent::getModPanelWidth() const {
    // Width for 2 columns of mod knobs (2x4 grid)
    return 130;
}

void RackComponent::paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw "MODS" header
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("MODS", panelArea.removeFromTop(16), juce::Justification::centred);
}

void RackComponent::resizedModPanel(juce::Rectangle<int> panelArea) {
    DBG("RackComponent::resizedModPanel called, panelArea=" << panelArea.toString());
    panelArea.removeFromTop(16);  // Skip "MODS" header

    if (modsPanel_) {
        modsPanel_->setBounds(panelArea);
        modsPanel_->setVisible(true);
        DBG("  modsPanel_ bounds set to " << panelArea.toString() << ", visible=true");
        updateModsPanel();
    }
}

}  // namespace magda::daw::ui
